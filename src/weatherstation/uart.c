#include "uart.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <string.h>

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#define UBRR_VALUE ((F_CPU/16/UART_BAUD)-1)

// ======================================================
// UART 9600 8N1
// RX por interrupcion con buffer circular: no se pierden
// bytes aunque otra tarea este bloqueando (p. ej. la
// lectura del DS18B20). TX sigue siendo por polling.
// ======================================================

static volatile uint8_t rx_buf[UART_RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;   // posicion de escritura (ISR)
static volatile uint8_t rx_tail = 0;   // posicion de lectura (main)

void uart_init(void)
{
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;

    UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0);
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
}

ISR(USART_RX_vect)
{
    uint8_t next = (rx_head + 1) & (UART_RX_BUF_SIZE - 1);

    if(next != rx_tail)
    {
        rx_buf[rx_head] = UDR0;
        rx_head = next;
    }
    else
    {
        // Buffer lleno: se descarta el byte (la lectura de UDR0
        // limpia el flag RXC).
        (void)UDR0;
    }
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

void uart_tx_string_P(const char* s)
{
    char c;

    while((c = (char)pgm_read_byte(s++)) != '\0')
        uart_tx_char(c);
}

uint8_t uart_rx_available(void)
{
    return (uint8_t)((rx_head - rx_tail) & (UART_RX_BUF_SIZE - 1));
}

uint8_t uart_rx_read(void)
{
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & (UART_RX_BUF_SIZE - 1);
    return c;
}

void uart_tx_decimal(int16_t v)
{
    char tmp[8];
    uint16_t u;

    if(v < 0)
    {
        uart_tx_char('-');
        u = (uint16_t)(-v);
    }
    else
        u = (uint16_t)v;

    itoa(u / 10, tmp, 10);
    uart_tx_string(tmp);
    uart_tx_char('.');
    itoa(u % 10, tmp, 10);
    uart_tx_string(tmp);
}

void uart_tx_uint32(uint32_t v)
{
    char tmp[11];

    ultoa(v, tmp, 10);
    uart_tx_string(tmp);
}

void uart_tx_time(uint32_t seconds)
{
    char line[12]={0};

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

    uart_tx_string(line);
}


