#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <compat/deprecated.h>
#include <stdlib.h>
#include <string.h>

// ======================================================
// CONFIG
// ======================================================

#define BAUD 9600
#define UBRR_VALUE ((F_CPU/16/BAUD)-1)

#define FACTOR_KMH 3.6f

#define MIN_PERIOD 100

#define MAX_OVERFLOWS_NO_SIGNAL 5

#define RAIN_DEBOUNCE_MS 100

#define RAIN_MM_PER_TIP 0.2f

// ======================================================
// ANEMOMETRO
// ======================================================

volatile uint16_t last_capture = 0;
volatile uint16_t period = 0;

volatile uint8_t new_data = 0;
volatile uint8_t overflow_counter = 0;

float velocidad = 0;

// ======================================================
// PLUVIOMETRO
// ======================================================

volatile uint32_t rain_count = 0;
volatile uint32_t rain_last_pulse = 0;

// ======================================================
// TIMER MILLISECONDS
// ======================================================

volatile uint32_t millis_counter = 0;

// ======================================================
// UART
// ======================================================

void uart_init(void)
{
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;

    UCSR0B = (1<<RXEN0) | (1<<TXEN0);
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
}

void uart_tx_char(char c)
{
    while(!(UCSR0A & (1<<UDRE0)));
    UDR0 = c;
}

void uart_tx_string(const char* s)
{
    while(*s)
        uart_tx_char(*s++);
}

uint8_t uart_rx_available(void)
{
    return (UCSR0A & (1<<RXC0));
}

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

    millis_counter++;

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
        uint32_t now = millis_counter;

        if((now - rain_last_pulse) > RAIN_DEBOUNCE_MS)
        {
            rain_count++;
            rain_last_pulse = now;
        }
    }

    old_state = new_state;
}

// ======================================================
// IO
// ======================================================

void io_init(void)
{
    DDRD = (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
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

    sei();

    float frequency = 0;
    char buffer[32];
    char rx_buffer[32];
    uint8_t rx_index = 0;

    uart_tx_string("*** ATMEGA328P WEATHER 1.0\r\n");


    while(1)
    {
        // ====================================
        // ANEMOMETRO
        // ====================================

        if(overflow_counter > MAX_OVERFLOWS_NO_SIGNAL)
        {
            velocidad = 0;
        }

        if(new_data)
        {
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

        // ====================================
        // SERIAL COMMANDS
        // ====================================

        while(uart_rx_available())
        {
            char c = UDR0;

			uart_tx_char(c);

            if(c == '\r' || c == '\n')
            {
                rx_buffer[rx_index] = 0;
                rx_index = 0;

                if(strcmp(rx_buffer,"@STAT_ANEMO") == 0)
                {
                    dtostrf(velocidad,6,3,buffer);

                    uart_tx_string("ANEMO=");
                    uart_tx_string(buffer);
                    uart_tx_string(" km/h\r\n");
                }
                else if(strcmp(rx_buffer,"@STAT_RAIN") == 0)
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

                    uart_tx_string("RAIN_LM2=");

                    dtostrf(mm,6,2,buffer);
                    uart_tx_string(buffer);

                    uart_tx_string("\r\n");
                }
                else if(strcmp(rx_buffer,"@RESET_RAIN") == 0)
                {
                    cli();
                    rain_count = 0;
                    sei();

                    uart_tx_string("RAIN RESET OK\r\n");
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
}