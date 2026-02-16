#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <compat/deprecated.h>
#include <stdlib.h>

// =====================
// CONFIG
// =====================

#define BAUD 9600
#define UBRR_VALUE ((F_CPU/16/BAUD)-1)

#define FACTOR_KMH 3.6f    // 0.001f

// filtro anti ruido (ajustar segun sensor)
#define MIN_PERIOD 100   // ticks timer (50us)

// nº overflows antes de declarar velocidad 0
#define MAX_OVERFLOWS_NO_SIGNAL 5

// =====================
// VARIABLES
// =====================

volatile uint16_t last_capture = 0;
volatile uint16_t period = 0;

volatile uint8_t new_data = 0;
volatile uint8_t overflow_counter = 0;

// =====================
// UART
// =====================

void uart_init(void)
{
	UBRR0H = (UBRR_VALUE >> 8);
	UBRR0L = UBRR_VALUE;

	UCSR0B = (1<<TXEN0);
	UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);
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

// =====================
// TIMER1 INPUT CAPTURE
// =====================

void timer1_init(void)
{
	DDRB &= ~(1<<DDB0); // ICP1 input

	TCCR1A = 0;

	// noise canceler + rising edge + prescaler 8
	TCCR1B = (1<<ICNC1)|(1<<ICES1)|(1<<CS11);

	// enable interrupts
	TIMSK1 = (1<<ICIE1)|(1<<TOIE1);
}

// input capture interrupt
ISR(TIMER1_CAPT_vect)
{
	uint16_t capture = ICR1;

	uint16_t diff = capture - last_capture;
	last_capture = capture;

	// filtro anti ruido
	if(diff > MIN_PERIOD)
	{
		period = diff;
		new_data = 1;
		overflow_counter = 0;
	}
}

// overflow interrupt
ISR(TIMER1_OVF_vect)
{
	overflow_counter++;
}

// =====================
// MAIN
// =====================

int main(void)
{
	PORTD = 0x00;
	DDRD = 0b00100000;
	
	uart_init();
	timer1_init();

	
	sei();

	float frequency = 0;
	float velocidad = 0;
	char buffer[32];

	while(1)
	{
		// detectar ausencia de señal
		if(overflow_counter > MAX_OVERFLOWS_NO_SIGNAL)
		{
			velocidad = 0;
		}

		if(new_data)
		{
			sbi(PORTD, PD5);
			
			new_data = 0;

			if(period != 0)
			{
				frequency = 1000000.0f / period;
				velocidad = frequency * FACTOR_KMH;
				cbi(PORTD, PD5);
			}
		}

		dtostrf(velocidad,6,3,buffer);

		uart_tx_string("Velocidad km/h: ");
		uart_tx_string(buffer);
		uart_tx_string("\r\n");

		// pequeño delay sin bloquear timer
		for(uint32_t i=0;i<50000;i++)
			__asm__("nop");
	}
}