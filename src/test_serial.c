#define F_CPU 8000000UL

#include <avr/io.h>

#define BAUD 9600
#define UBRR_VALUE ((F_CPU/16/BAUD)-1)

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

int main(void)
{
    uart_init();

    while(1)
    {
        if(UCSR0A & (1<<RXC0))
        {
            uart_tx_char(UDR0);
        }
    }
}