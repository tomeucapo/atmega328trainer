#include <avr/io.h>
#include "spi.h"

// ======================================================
// SPI maestro. Los pines PB3/PB4/PB5 son tambien los del ISP:
// al programar, el SX127x debe tener CS en alto (pull-up externo
// recomendado) para no cargar el bus.
// F_CPU/16 = 500 kHz con 8 MHz; el SX127x admite hasta 10 MHz.
// ======================================================

void spi_init(void)
{
    DDRB |= (1 << DDB3) | (1 << DDB5);
    DDRB &= ~(1 << DDB4);

    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
    SPSR = 0;
}

uint8_t spi_transfer(uint8_t data)
{
    SPDR = data;

    while(!(SPSR & (1 << SPIF)))
        ;

    return SPDR;
}
