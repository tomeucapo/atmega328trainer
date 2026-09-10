#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <avr/io.h> 

#define DS18B20_DDR  DDRB
#define DS18B20_PORT PORTB
#define DS18B20_PIN  PINB
#define DS18B20_BIT  PB2

void onewire_low(void);
void onewire_release(void);
uint8_t onewire_read_pin(void);
uint8_t onewire_reset(void);
void onewire_write_bit(uint8_t bit);
uint8_t onewire_read_bit(void);
void onewire_write_byte(uint8_t byte);
uint8_t onewire_read_byte(void);

#endif
