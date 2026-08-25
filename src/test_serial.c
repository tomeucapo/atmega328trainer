#define F_CPU 8000000UL

#include <avr/io.h>

int main(void)
{
    DDRD |= (1<<DDD6);

    while(1)
    {
        if(PIND & (1<<PIND0))
            PORTD |= (1<<PORTD6);
        else
            PORTD &= ~(1<<PORTD6);
    }
}