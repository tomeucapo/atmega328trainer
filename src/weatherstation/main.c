#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
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
#include "bme280.h"
#include "prediction.h"
#include "sx127x.h"
#include "lora_proto.h"

// ======================================================
// CONFIG
// ======================================================

#define FACTOR_KMH 3.2f
#define MIN_PERIOD 100
#define MAX_OVERFLOWS_NO_SIGNAL 5
#define RAIN_DEBOUNCE_MS 100
#define RAIN_MM_PER_TIP 0.3f

// Historial de temperatura para la grafica del OLED
#define TEMP_HISTORY_LEN 64               // potencia de 2 (2 px por muestra)
#define TEMP_HIST_EMPTY ((int8_t)-128)    // marca de "sin dato"
#define TEMP_SAMPLE_MS 15000              // una muestra cada 15 s

// Boton en el MCP23017 (GPA7, pull-up interno, activo a GND)
#define BTN_MCP_PORT MCP23017_PORTA
#define BTN_MCP_PIN 7

// LED GPB0 del MCP23017: replica el parpadeo de PD6
static volatile uint8_t mcp_led_state = 0;
static uint8_t mcp_present = 0;

// ======================================================
// ANEMOMETRO
// ======================================================

volatile uint16_t last_capture = 0;
volatile uint16_t period = 0;

volatile uint8_t new_data = 0;
volatile uint8_t overflow_counter = 0;

float velocidad = 0;
float current_temp = DS18B20_ERROR_TEMP;
float bme_temp = 0;
float bme_hum = 0;
float bme_press = 0;
uint8_t bme_present = 0;

// ======================================================
// NODO REMOTO LORA (BME280 exterior)
// ======================================================

#define LORA_TIMEOUT_MS 180000UL   // 3 minutos sin tramas

float ext_temp = 0;
float ext_hum = 0;
float ext_press = 0;
uint32_t ext_last = 0;
uint8_t ext_ok = 0;
uint8_t lora_present = 0;
uint16_t lora_seq = 0;
uint16_t lora_bad = 0;

// ======================================================
// PLUVIOMETRO
// ======================================================

volatile uint32_t rain_count = 0;
volatile uint32_t rain_last_pulse = 0;

// ======================================================
// VISTA OLED Y HISTORIAL DE TEMPERATURA
// ======================================================

static int8_t temp_history[TEMP_HISTORY_LEN];
static uint8_t temp_hist_head = 0;
static uint8_t view_mode = 0;   // 0 = texto, 1 = grafica

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
        mcp_led_state ^= 1;
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

        if(value > 7)
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

    char hours[4]={0};
    ultoa((seconds/60/60)%24, hours, 10);
    strcat(line, hours);
    strcat(line, ":");
    
    char minutes[4]={0};
    ultoa((seconds/60)%60, minutes, 10);
    strcat(line, minutes);
    strcat(line, ":");
    
    char seconds_str[4]={0};
    ultoa(seconds%60, seconds_str, 10);
    strcat(line, seconds_str);

    ssd1306_draw_string(7, 1, line);
}

// Temperatura a mostrar: DS18B20 exterior, nodo LoRa de respaldo
// y, por ultimo, el BME280 interior.
static float oled_temp(void)
{
    if(current_temp != DS18B20_ERROR_TEMP)
        return current_temp;

    if(ext_ok)
        return ext_temp;

    if(bme_present && bme_temp > -50.0f)
        return bme_temp;

    return DS18B20_ERROR_TEMP;
}

// Grafica de temperatura: 128 muestras (1 px por columna), rango
// automatico min/max, lineas verticales conectando muestras seguidas.
void update_oled_graph(void)
{
    char line[24] = {0};
    char tmin[8], tmax[8];
    int8_t vmin = 127, vmax = -128;
    uint8_t i, x;
    uint8_t prev_y = 0, have_prev = 0;
    float tdisp = oled_temp();

    ssd1306_clear();

    strcpy_P(line, PSTR("TEMP "));
    if(tdisp == DS18B20_ERROR_TEMP)
    {
        strcat_P(line, PSTR("--.- C"));
    }
    else
    {
        char t[8];
        dtostrf(tdisp, 4, 1, t);
        strcat(line, t);
        strcat_P(line, PSTR(" C"));
    }
    ssd1306_draw_string_centered(0, line);

    for(i = 0; i < TEMP_HISTORY_LEN; i++)
    {
        int8_t v = temp_history[i];

        if(v == TEMP_HIST_EMPTY)
            continue;

        if(v < vmin) vmin = v;
        if(v > vmax) vmax = v;
    }

    if(vmax < vmin)
    {
        ssd1306_draw_string_centered_P(3, PSTR("SIN DATOS"));
        ssd1306_update();
        return;
    }

    if(vmax == vmin)
        vmax = vmin + 1;

    // Grafica en y = 8..55 (48 px de alto); pagina 0 y 7 para texto.
    // Cada muestra ocupa 2 px de ancho.
    for(x = 0; x < TEMP_HISTORY_LEN; x++)
    {
        uint8_t idx = (uint8_t)((temp_hist_head + x) & (TEMP_HISTORY_LEN - 1));
        int8_t v = temp_history[idx];
        uint8_t px = (uint8_t)(x * 2);
        uint8_t y;

        if(v == TEMP_HIST_EMPTY)
        {
            have_prev = 0;
            continue;
        }

        y = (uint8_t)(8 + (uint16_t)(vmax - v) * 47 / (vmax - vmin));

        if(have_prev)
        {
            uint8_t y0 = prev_y, y1 = y, yy;

            if(y0 > y1)
            {
                uint8_t swap = y0;
                y0 = y1;
                y1 = swap;
            }

            for(yy = y0; yy <= y1; yy++)
            {
                ssd1306_draw_pixel(px, yy);
                ssd1306_draw_pixel((uint8_t)(px + 1), yy);
            }
        }
        else
        {
            ssd1306_draw_pixel(px, y);
            ssd1306_draw_pixel((uint8_t)(px + 1), y);
        }

        prev_y = y;
        have_prev = 1;
    }

    // Pie: min..max del rango mostrado
    dtostrf(vmin, 3, 0, tmin);
    dtostrf(vmax, 3, 0, tmax);
    strcpy(line, tmin);
    strcat_P(line, PSTR(".."));
    strcat(line, tmax);
    strcat_P(line, PSTR(" C"));
    ssd1306_draw_string(7, 34, line);

    ssd1306_update();
}

void update_oled(void)
{
    if(view_mode)
    {
        update_oled_graph();
        return;
    }

    char line[24] = {0};
    float rain_mm;
    float tdisp = oled_temp();

    ssd1306_clear();

    ssd1306_draw_string_centered_P(0, PSTR("ANEMOMETRO"));

    dtostrf(velocidad, 5, 1, line);
    strcat_P(line, PSTR(" km/h"));
    ssd1306_draw_string_centered(1, line);

    ssd1306_draw_string_centered_P(2, PSTR("LLUVIA"));

    // rain_count es uint32 modificado en la ISR: lectura atomica
    cli();
    rain_mm = rain_count * RAIN_MM_PER_TIP;
    sei();

    dtostrf(rain_mm, 4, 2, line);
    strcat_P(line, PSTR(" mm"));
    ssd1306_draw_string_centered(3, line);

    ssd1306_draw_string_centered_P(4, PSTR("TEMP"));

    if(tdisp == DS18B20_ERROR_TEMP)
        ssd1306_draw_string_centered_P(5, PSTR("--.- C"));
    else
    {
        dtostrf(tdisp, 4, 1, line);
        strcat_P(line, PSTR(" C"));
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
static void push_temp_sample(float t)
{
    int8_t v;

    if(t == DS18B20_ERROR_TEMP)
    {
        v = TEMP_HIST_EMPTY;
    }
    else
    {
        int16_t r = (t >= 0.0f) ? (int16_t)(t + 0.5f) : (int16_t)(t - 0.5f);

        if(r > 127) r = 127;
        if(r < -127) r = -127;

        v = (int8_t)r;
    }

    temp_history[temp_hist_head] = v;
    temp_hist_head = (uint8_t)((temp_hist_head + 1) & (TEMP_HISTORY_LEN - 1));
}

static void task_sensors(void)
{
    static uint32_t last_sample = 0;
    uint32_t now;
    bme280_data_t bd;

    if(!uart_rx_available())
    {
        current_temp = ds18b20_get_temp();
        if(bme_present)
        {
            if(bme280_read(&bd) == 0)
            {
                bme_temp = bd.temperature;
                bme_hum = bd.humidity;
                bme_press = bd.pressure;
            }
        }
    }

    now = sched_get_tick();

    if((now - last_sample) >= TEMP_SAMPLE_MS)
    {
        float tshow = current_temp;

        last_sample = now;

        // Prioridad: DS18B20 exterior, nodo LoRa, BME280 interior.
        if(tshow == DS18B20_ERROR_TEMP && ext_ok)
            tshow = ext_temp;
        if(tshow == DS18B20_ERROR_TEMP && bme_present && bme_temp > -50.0f)
            tshow = bme_temp;

        push_temp_sample(tshow);
    }
}

// Log: guarda una lectura periodica en la EEPROM I2C. La
// escritura bloquea ~5 ms mientras el chip se programa.
static void task_log(void)
{
    uint32_t rain;
    float tlog = current_temp;
    float hlog = 0.0f;
    float plog = 0.0f;

    // Humedad/presion exterior del nodo LoRa; si no hay enlace,
    // se usan las del BME280 interior como respaldo.
    if(ext_ok)
    {
        hlog = ext_hum;
        plog = ext_press;

        if(tlog == DS18B20_ERROR_TEMP)
            tlog = ext_temp;
    }
    else if(bme_present)
    {
        hlog = bme_hum;
        plog = bme_press;

        if(tlog == DS18B20_ERROR_TEMP && bme_temp > -50.0f)
            tlog = bme_temp;
    }

    cli();
    rain = rain_count;
    sei();

    eeprom_log_ext(sched_get_tick() / 1000, velocidad, tlog, hlog, plog, rain);
    pred_add_sample(tlog, hlog, plog, velocidad, rain * RAIN_MM_PER_TIP);
}

// OLED: envio del framebuffer por I2C; se omite si hay bytes
// de UART pendientes para no robarles tiempo al puerto serie.
static void task_display(void)
{
    if(uart_rx_available())
        return;

    update_oled();
}

// Boton MCP23017 (GPA7): con pull-up interno, reposo en alto y
// pulsado a GND. Se exigen 3 lecturas seguidas distintas (~150 ms)
// para confirmar el cambio de estado.
static void task_button(void)
{
    static uint8_t stable = 1;
    static uint8_t counter = 0;

    uint8_t raw = mcp23017_read_pin(BTN_MCP_PORT, BTN_MCP_PIN);

    if(raw == stable)
    {
        counter = 0;
        return;
    }

    if(++counter >= 3)
    {
        stable = raw;
        counter = 0;

        if(stable == 0)
            view_mode ^= 1;
    }
}

// LED GPB0 del MCP23017: se actualiza desde el estado que togglea
// la ISR de TIMER0 junto con PD6 (1 Hz).
static void task_mcp_led(void)
{
    static uint8_t last_state = 0;

    if(!mcp_present)
        return;

    if(mcp_led_state == last_state)
        return;

    last_state = mcp_led_state;
    mcp23017_write_pin(MCP23017_PORTB, 0, last_state);
}

// LoRa: recoge tramas del nodo remoto (temperatura, humedad,
// presion y bateria) y marca el enlace como caido si dejan de
// llegar durante LORA_TIMEOUT_MS.
static void task_lora(void)
{
    uint8_t buf[LORA_WEATHER_SIZE];
    uint8_t len = sizeof(buf);
    uint32_t now;

    if(!lora_present)
        return;

    if(sx127x_poll_rx(buf, &len))
    {
        lora_weather_t w;

        if(lora_parse_weather(buf, len, &w))
        {
            ext_temp = w.temp_x10 / 10.0f;
            ext_hum = w.hum_x10 / 10.0f;
            ext_press = w.press_hpa;
            ext_last = sched_get_tick();
            ext_ok = 1;
            lora_seq = w.seq;
        }
        else
        {
            lora_bad++;
        }
    }

    now = sched_get_tick();

    if(ext_ok && (now - ext_last) > LORA_TIMEOUT_MS)
        ext_ok = 0;
}

// @MCP_ON / @MCP_OFF <pin> | A<pin> | B<pin>
// Puerto por defecto A; el pin GPA7 esta reservado (pulsador a masa).
// Devuelve 1 si aplico el cambio, 0 (con ERR) si el pin no es valido.
static uint8_t handle_mcp_cmd(const char *arg, uint8_t value)
{
    char buffer[8];
    char port;
    uint8_t pin;

    while(*arg == ' ')
        arg++;

    if(*arg == 'B' || *arg == 'b')
    {
        port = 'B';
        arg++;
    }
    else
    {
        port = 'A';

        if(*arg == 'A' || *arg == 'a')
            arg++;
    }

    if(!parse_mcp_pin(arg, &pin) ||
       (port == 'A' && pin == BTN_MCP_PIN))
    {
        uart_tx_string_P(PSTR("ERR\r\n"));
        return 0;
    }

    mcp23017_write_pin(port == 'B' ? MCP23017_PORTB : MCP23017_PORTA,
                       pin, value);

    if(value)
        uart_tx_string_P(PSTR("MCP_ON="));
    else
        uart_tx_string_P(PSTR("MCP_OFF="));

    buffer[0] = port;
    buffer[1] = 0;
    uart_tx_string(buffer);

    utoa(pin, buffer, 10);
    uart_tx_string(buffer);
    uart_tx_string_P(PSTR("\r\n"));

    return 1;
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

                uart_tx_string_P(PSTR("ANEMO="));
                uart_tx_string(buffer);
                uart_tx_string_P(PSTR(" km/h\r\n"));
            }
            else if(strcmp(rx_buffer,"@GET_RAIN") == 0)
            {
                uint32_t count;
                float mm;

                cli();
                count = rain_count;
                sei();
                mm = count * RAIN_MM_PER_TIP;

                uart_tx_string_P(PSTR("RAIN_COUNT="));

                ultoa(count,buffer,10);
                uart_tx_string(buffer);

                uart_tx_string_P(PSTR("\r\n"));

                uart_tx_string_P(PSTR("RAIN_MM="));

                dtostrf(mm,6,2,buffer);
                uart_tx_string(buffer);

                uart_tx_string_P(PSTR("\r\n"));
            }
            else if(strcmp(rx_buffer,"@GET_TEMP") == 0)
            {
                float temp = ds18b20_get_temp();

                if(temp == DS18B20_ERROR_TEMP)
                {
                    uart_tx_string_P(PSTR("TEMP_ERR\r\n"));
                }
                else
                {
                    uart_tx_string_P(PSTR("TEMP="));
                    dtostrf(temp,6,3,buffer);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR(" C\r\n"));
                }
            }
            else if(strcmp(rx_buffer,"@RESET_RAIN") == 0)
            {
                cli();
                rain_count = 0;
                sei();

                uart_tx_string_P(PSTR("RAIN RESET OK\r\n"));
            }
            else if(strcmp(rx_buffer,"@DUMP") == 0)
            {
                eeprom_dump();
            }
            else if(strcmp(rx_buffer,"@VIEW") == 0)
            {
                view_mode ^= 1;

                if(view_mode)
                    uart_tx_string_P(PSTR("VIEW=GRAPH\r\n"));
                else
                    uart_tx_string_P(PSTR("VIEW=TEXT\r\n"));
            }
            else if(strcmp(rx_buffer,"@GET_BME") == 0)
            {
                if(!bme_present)
                {
                    uart_tx_string_P(PSTR("BME280_NOT_FOUND\r\n"));
                }
                else
                {
                    char pb[16];
                    uart_tx_string_P(PSTR("BME_TEMP="));
                    dtostrf(bme_temp,6,2,pb);
                    uart_tx_string(pb);
                    uart_tx_string_P(PSTR(" C\r\nBME_HUM="));
                    dtostrf(bme_hum,5,1,pb);
                    uart_tx_string(pb);
                    uart_tx_string_P(PSTR(" %RH\r\nBME_PRESS="));
                    dtostrf(bme_press,7,2,pb);
                    uart_tx_string(pb);
                    uart_tx_string_P(PSTR(" hPa\r\n"));
                    float dp = pred_dewpoint(bme_temp > -50 ? bme_temp : current_temp, bme_hum);
                    uart_tx_string_P(PSTR("DEWPOINT="));
                    dtostrf(dp,6,2,pb);
                    uart_tx_string(pb);
                    uart_tx_string_P(PSTR(" C\r\n"));
                }
            }
            else if(strcmp(rx_buffer,"@GET_EXT") == 0)
            {
                if(!lora_present)
                {
                    uart_tx_string_P(PSTR("LORA_NOT_FOUND\r\n"));
                }
                else if(!ext_ok)
                {
                    uart_tx_string_P(PSTR("EXT_LINK=NO\r\n"));
                }
                else
                {
                    uart_tx_string_P(PSTR("EXT_LINK=OK\r\nEXT_AGE="));
                    ultoa((sched_get_tick() - ext_last) / 1000, buffer, 10);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR(" s\r\nEXT_TEMP="));
                    dtostrf(ext_temp,6,2,buffer);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR(" C\r\nEXT_HUM="));
                    dtostrf(ext_hum,5,1,buffer);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR(" %RH\r\nEXT_PRESS="));
                    dtostrf(ext_press,7,2,buffer);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR(" hPa\r\nEXT_SEQ="));
                    ultoa(lora_seq, buffer, 10);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR("\r\nEXT_BAD="));
                    ultoa(lora_bad, buffer, 10);
                    uart_tx_string(buffer);
                    uart_tx_string_P(PSTR("\r\n"));
                }
            }
            else if(strcmp(rx_buffer,"@FORECAST") == 0)
            {
                char fbuf[48];
                pred_forecast(fbuf, sizeof(fbuf));
                uart_tx_string(fbuf);
                uart_tx_string_P(PSTR("\r\n"));
            }
            else if(strncmp(rx_buffer,"@MCP_ON",7) == 0)
            {
                handle_mcp_cmd(rx_buffer + 7, 1);
            }
            else if(strncmp(rx_buffer,"@MCP_OFF",8) == 0)
            {
                handle_mcp_cmd(rx_buffer + 8, 0);
            }
            else
            {
                uart_tx_string_P(PSTR("ERR\r\n"));
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
    { task_button,   50,      0 },
    { task_mcp_led, 100,      0 },
    { task_serial,    5,      0 },
    { task_lora,    100,      0 },
    { task_display, 500,      0 },
    { task_sensors, 2000,   100 },
    { task_log,    60000,  3000 },
};

void welcome(void)
{
        uint8_t i;

        for(i = 0; i <= SSD1306_PAGES; i++)
        {
            ssd1306_clear();
            if(i < SSD1306_PAGES)
                ssd1306_draw_string_centered_P(i, PSTR("Weather Station 1.0"));
            if(i > 0)
                ssd1306_draw_string_centered_P(i - 1, PSTR("TCC 2026(C)"));
            ssd1306_update();
            _delay_ms(120);
        }

        _delay_ms(2000);

        for(i = SSD1306_PAGES; i > 0; i--)
        {
            ssd1306_clear();
            if(i <= SSD1306_PAGES)
                ssd1306_draw_string_centered_P(i - 1, PSTR("Weather Station 1.0"));
            if(i < SSD1306_PAGES)
                ssd1306_draw_string_centered_P(i, PSTR("TCC 2026(C)"));
            ssd1306_update();
            _delay_ms(120);
        }
}


// ======================================================
// MAIN
// ======================================================

int main(void)
{
    uint8_t i;

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

    // MCP23017 (PC4=SDA, PC5=SCL). mcp23017_init() ya deja GPA7 como
    // entrada (pulsador a masa) y el resto como salidas.
    mcp23017_init();
    mcp23017_pullup(MCP23017_PORTA, (1 << BTN_MCP_PIN)); // pull-up en GPA7
    mcp23017_direction(MCP23017_PORTB, 0x00); // LEDs: salidas

    // Detecta el MCP23017 en el bus I2C (ACK en su direccion)
    mcp_present = mcp23017_detect();

    // Historial de temperatura vacio
    for(i = 0; i < TEMP_HISTORY_LEN; i++)
        temp_history[i] = TEMP_HIST_EMPTY;

    // OLED SSD1306 en el mismo bus I2C
    ssd1306_init();

    // EEPROM 24AA515 en el mismo bus I2C
    eeprom_init();

    // BME280 interior
    bme_present = bme280_init();
    pred_init();

    // Nodo remoto LoRa (SX1276/78 por SPI en PD2=CS, PD3=RESET)
    lora_present = sx127x_init();

    welcome();

    sei();

    sched_init(task_list, (uint8_t)(sizeof(task_list) / sizeof(task_list[0])));

    uart_tx_string_P(PSTR("*** ATMEGA328P WEATHER 1.0\r\n"));

    if(mcp_present)
    {
        char addr[5] = "0x";

        utoa(MCP23017_I2C_ADDR, addr + 2, 16);

        uart_tx_string_P(PSTR("MCP23017 DETECTED I2C ADDR="));
        uart_tx_string(addr);
        uart_tx_string_P(PSTR("\r\n"));
    }
    else
    {
        uart_tx_string_P(PSTR("MCP23017 NOT DETECTED\r\n"));
    }
    if(bme_present)
    {
        uart_tx_string_P(PSTR("BME280 DETECTED\r\n"));
    }
    else
    {
        uart_tx_string_P(PSTR("BME280 NOT DETECTED\r\n"));
    }
    if(lora_present)
    {
        uart_tx_string_P(PSTR("SX127X DETECTED\r\n"));
    }
    else
    {
        uart_tx_string_P(PSTR("SX127X NOT DETECTED\r\n"));
    }

    while(1)
    {
        sched_manage();
    }
}
