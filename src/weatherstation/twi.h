#ifndef TWI_H
#define TWI_H

#include <stdint.h>
#include <avr/io.h>

// SCL = F_CPU / (16 + 2*TWBR*prescaler)
// Con F_CPU 8 MHz y prescaler 1x -> 100 kHz
#define TWBR_VALUE 32

void twi_init(void);
uint8_t twi_start(void);              // 0 = OK
uint8_t twi_restart(void);            // 0 = OK
uint8_t twi_write_byte(uint8_t data); // 0 = ACK, 1 = NACK/error
uint8_t twi_read_ack(uint8_t *data);  // 0 = OK
uint8_t twi_read_nack(uint8_t *data); // 0 = OK (ultimo byte de la lectura)
void twi_stop(void);

#endif