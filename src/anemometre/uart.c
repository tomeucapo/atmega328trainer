#include "uart.h"
#include <avr/io.h>
#include <avr/interrupt.h>

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