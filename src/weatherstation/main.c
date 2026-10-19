#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <compat/deprecated.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <util/delay.h>
#include "ds18b20.h"
#include "mcp23017.h"
#include "ssd1306.h"
#include "scheduler.h"
#include "uart.h"
#include "eeprom.h"
#include "twi.h"

// ======================================================
// CONFIG
// ======================================================

#define FACTOR_KMH 3.2f
#define MIN_PERIOD 100
#define MAX_OVERFLOWS_NO_SIGNAL 5
#define RAIN_DEBOUNCE_MS 100
#define RAIN_MM_PER_TIP 0.3f

// ======================================================
// ANEMOMETRO
// ======================================================

volatile uint16_t last_capture = 0;
volatile uint16_t period = 0;

volatile uint8_t new_data = 0;
volatile uint8_t overflow_counter = 0;

float velocidad = 0;
float current_temp = DS18B20_ERROR_TEMP;

// ======================================================
// PLUVIOMETRO
// ======================================================

volatile uint32_t rain_count = 0;
volatile uint32_t rain_last_pulse = 0;

// ======================================================
// TIMER1 INPUT CAPTURE (ANEMOMETRO)
// ======================================================

void timer1_init(void)
{
    DDRB &= ~(1<<DDB0);

    TCCR1A = 0;

    TCCR1B =
        (1<<ICNC1) |
        (1<<ICES1) |
        (1<<CS11);

    TIMSK1 =
        (1<<ICIE1) |
        (1<<TOIE1);
}

ISR(TIMER1_CAPT_vect)
{
    uint16_t capture = ICR1;
    uint16_t diff = capture - last_capture;

    last_capture = capture;

    if(diff > MIN_PERIOD)
    {
        period = diff;
        new_data = 1;
        overflow_counter = 0;
    }
}

ISR(TIMER1_OVF_vect)
{
    overflow_counter++;
}

// ======================================================
// TIMER0 1 ms
// ======================================================

void timer0_init(void)
{
    TCCR0A = (1<<WGM01);

    OCR0A = 124;

    TCCR0B =
        (1<<CS01) |
        (1<<CS00);

    TIMSK0 = (1<<OCIE0A);
}

ISR(TIMER0_COMPA_vect)
{
    static uint16_t blink_counter = 0;

    sched_tick();

    if(++blink_counter >= 500)
    {
        PORTD ^= (1<<PORTD6);
        blink_counter = 0;
    }
}

// ======================================================
// PLUVIOMETRO
// PB1 = PCINT1
// CONTACTO NC
// ======================================================

void rain_init(void)
{
    DDRB &= ~(1<<DDB1);
    PORTB |= (1<<PORTB1);
    PCICR |= (1<<PCIE0);
    PCMSK0 |= (1<<PCINT1);
}

ISR(PCINT0_vect)
{
    static uint8_t old_state = 0;

    uint8_t new_state = (PINB & (1<<PINB1)) ? 1 : 0;

    // NC -> contar apertura

    if(!old_state && new_state)
    {
        uint32_t now = sched_get_tick();

        if((now - rain_last_pulse) > RAIN_DEBOUNCE_MS)
        {
            rain_count++;
            rain_last_pulse = now;
        }
    }

    old_state = new_state;
}

void calculate_anemo_velocity()
{
        if(overflow_counter > MAX_OVERFLOWS_NO_SIGNAL)
            velocidad = 0;
        
        if(!new_data)
            return;
        
        float frequency = 0;
        uint16_t local_period;

        cli();
        local_period = period;
        new_data = 0;
        sei();

        if(local_period != 0)
        {
           frequency = 100000.0f / local_period;
           velocidad = frequency * FACTOR_KMH;
        }
}


// ======================================================
// IO
// ======================================================

void io_init(void)
{
    DDRD = (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
}

// ======================================================
// MCP23017 COMANDOS SERIE
// ======================================================

static uint8_t parse_mcp_pin(const char *s, uint8_t *pin)
{
    uint8_t value = 0;

    while(*s == ' ')
        s++;

    if(*s < '0' || *s > '9')
        return 0;

    while(*s >= '0' && *s <= '9')
    {
        value = value * 10 + (*s - '0');

        if(value > 15)
            return 0;

        s++;
    }

    if(*s != '\0')
        return 0;

    *pin = value;
    return 1;
}

// ======================================================
// OLED SSD1306
// ======================================================

void print_time(uint32_t seconds)
{
    char line[24]={0};

    char hours[2]={0};
    ultoa((seconds/60/60)%24, hours, 10);
    strcat(line, hours);
    strcat(line, ":");
    
    char minutes[2]={0};
    ultoa((seconds/60)%60, minutes, 10);
    strcat(line, minutes);
    strcat(line, ":");
    
    char seconds_str[2]={0};
    ultoa(seconds%60, seconds_str, 10);
    strcat(line, seconds_str);

    ssd1306_draw_string(7, 1, line);
}

void update_oled(void)
{
    char line[24] = {0};
    float rain_mm;

    ssd1306_clear();

    ssd1306_draw_string_centered(0, "ANEMOMETRO");

    dtostrf(velocidad, 5, 1, line);
    strcat(line, " km/h");
    ssd1306_draw_string_centered(1, line);

    ssd1306_draw_string_centered(2, "LLUVIA");

    // rain_count es uint32 modificado en la ISR: lectura atomica
    cli();
    rain_mm = rain_count * RAIN_MM_PER_TIP;
    sei();

    dtostrf(rain_mm, 4, 2, line);
    strcat(line, " mm");
    ssd1306_draw_string_centered(3, line);

    ssd1306_draw_string_centered(4, "TEMP");

    if(current_temp == DS18B20_ERROR_TEMP)
        ssd1306_draw_string_centered(5, "--.- C");
    else
    {
        dtostrf(current_temp, 4, 1, line);
        strcat(line, " C");
        ssd1306_draw_string_centered(5, line);
    }

    print_time(sched_get_tick() / 1000);
    ssd1306_update();
}

// ======================================================
// TAREAS DEL SCHEDULER
// ======================================================

// Anemometro: recalcula la velocidad desde la ISR de ICP1
// (muy rapido, por eso periodo corto).
static void task_anemo(void)
{
    calculate_anemo_velocity();
}

// Sensores: la lectura del DS18B20 bloquea ~100 ms; se evita
// si hay bytes de UART pendientes.
static void task_sensors(void)
{
    if(!uart_rx_available())
        current_temp = ds18b20_get_temp();
}

// Log: guarda una lectura periodica en la EEPROM I2C. La
// escritura bloquea ~5 ms mientras el chip se programa.
static void task_log(void)
{
    uint32_t rain;

    cli();
    rain = rain_count;
    sei();

    eeprom_log(sched_get_tick() / 1000, velocidad, current_temp, rain);
}

// OLED: envio del framebuffer por I2C; se omite si hay bytes
// de UART pendientes para no robarles tiempo al puerto serie.
static void task_display(void)
{
    if(uart_rx_available())
        return;

    update_oled();
}

// UART: procesa los comandos serie terminados en CR/LF.
static void task_serial(void)
{
    static char buffer[32];
    static char rx_buffer[32];
    static uint8_t rx_index = 0;

    while(uart_rx_available())
    {
        char c = uart_rx_read();

        uart_tx_char(c);

        if(c == '\r' || c == '\n')
        {
            rx_buffer[rx_index] = 0;
            rx_index = 0;

            if(strcmp(rx_buffer,"@GET_ANEMO") == 0)
            {
                dtostrf(velocidad,6,3,buffer);

                uart_tx_string("ANEMO=");
                uart_tx_string(buffer);
                uart_tx_string(" km/h\r\n");
            }
            else if(strcmp(rx_buffer,"@GET_RAIN") == 0)
            {
                uint32_t count;
                float mm;

                cli();
                count = rain_count;
                sei();
                mm = count * RAIN_MM_PER_TIP;

                uart_tx_string("RAIN_COUNT=");

                ultoa(count,buffer,10);
                uart_tx_string(buffer);

                uart_tx_string("\r\n");

                uart_tx_string("RAIN_MM=");

                dtostrf(mm,6,2,buffer);
                uart_tx_string(buffer);

                uart_tx_string("\r\n");
            }
            else if(strcmp(rx_buffer,"@GET_TEMP") == 0)
            {
                float temp = ds18b20_get_temp();

                if(temp == DS18B20_ERROR_TEMP)
                {
                    uart_tx_string("TEMP_ERR\r\n");
                }
                else
                {
                    uart_tx_string("TEMP=");
                    dtostrf(temp,6,3,buffer);
                    uart_tx_string(buffer);
                    uart_tx_string(" C\r\n");
                }
            }
            else if(strcmp(rx_buffer,"@RESET_RAIN") == 0)
            {
                cli();
                rain_count = 0;
                sei();

                uart_tx_string("RAIN RESET OK\r\n");
            }
            else if(strcmp(rx_buffer,"@DUMP") == 0)
            {
                eeprom_dump();
            }
            else if(strncmp(rx_buffer,"@MCP_ON",7) == 0)
            {
                uint8_t pin;

                if(parse_mcp_pin(rx_buffer + 7, &pin))
                {
                    mcp23017_write_pin(pin / 8, pin % 8, 1);

                    uart_tx_string("MCP_ON=");
                    utoa(pin, buffer, 10);
                    uart_tx_string(buffer);
                    uart_tx_string("\r\n");
                }
                else
                {
                    uart_tx_string("ERR\r\n");
                }
            }
            else if(strncmp(rx_buffer,"@MCP_OFF",8) == 0)
            {
                uint8_t pin;

                if(parse_mcp_pin(rx_buffer + 8, &pin))
                {
                    mcp23017_write_pin(pin / 8, pin % 8, 0);

                    uart_tx_string("MCP_OFF=");
                    utoa(pin, buffer, 10);
                    uart_tx_string(buffer);
                    uart_tx_string("\r\n");
                }
                else
                {
                    uart_tx_string("ERR\r\n");
                }
            }
            else
            {
                uart_tx_string("ERR\r\n");
            }
        }
        else
        {
            if(rx_index < sizeof(rx_buffer)-1)
            {
                rx_buffer[rx_index++] = c;
            }
        }
    }
}

// Tabla de tareas: { funcion, periodo_ms, desfase_ms }. El desfase
// de task_sensors evita que su lectura bloqueante coincida con el
// refresco de la pantalla.
sched_task_t task_list[] = {
    { task_anemo,    50,      0 },
    { task_serial,    5,      0 },
    { task_display, 500,      0 },
    { task_sensors, 2000,   100 },
    { task_log,    60000,  3000 },
};

void welcome(void)
{
        const char *line1 = "Weather Station 1.0";
        const char *line2 = "TCC 2026(C)";
        uint8_t i;

        for(i = 0; i <= SSD1306_PAGES; i++)
        {
            ssd1306_clear();
            if(i < SSD1306_PAGES)
                ssd1306_draw_string_centered(i, line1);
            if(i > 0)
                ssd1306_draw_string_centered(i - 1, line2);
            ssd1306_update();
            _delay_ms(120);
        }

        _delay_ms(2000);

        for(i = SSD1306_PAGES; i > 0; i--)
        {
            ssd1306_clear();
            if(i <= SSD1306_PAGES)
                ssd1306_draw_string_centered(i - 1, line1);
            if(i < SSD1306_PAGES)
                ssd1306_draw_string_centered(i, line2);
            ssd1306_update();
            _delay_ms(120);
        }
}


// ======================================================
// MAIN
// ======================================================

int main(void)
{
    io_init();
    uart_init();
    timer1_init();
    timer0_init();
    rain_init();
    ds18b20_init();

    // Los esclavos I2C pueden arrancar despues que el MCU (o dejar
    // el bus en bajo al cortar la alimentacion): libera el bus y da
    // tiempo a que la OLED/EEPROM/MCP estabilicen su alimentacion.
    twi_bus_recover();
    _delay_ms(150);

    // MCP23017 (PC4=SDA, PC5=SCL)
    mcp23017_init();
    mcp23017_direction(MCP23017_PORTA, 0x00); // reles: salidas
    mcp23017_direction(MCP23017_PORTB, 0x00); // LEDs: salidas

    // OLED SSD1306 en el mismo bus I2C
    ssd1306_init();

    // EEPROM 24AA515 en el mismo bus I2C
    eeprom_init();

    welcome();

    sei();

    sched_init(task_list, (uint8_t)(sizeof(task_list) / sizeof(task_list[0])));

    uart_tx_string("*** ATMEGA328P WEATHER 1.0\r\n");

    while(1)
    {
        sched_manage();
    }
}
