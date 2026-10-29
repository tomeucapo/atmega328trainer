#ifndef SPI_H
#define SPI_H

#include <stdint.h>

// SPI hardware del ATmega328P (PB3=MOSI, PB4=MISO, PB5=SCK).
// El pin CS lo controla cada dispositivo (SX127x).
void spi_init(void);
uint8_t spi_transfer(uint8_t data);

#endif
