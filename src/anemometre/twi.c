#include <avr/io.h>
#include "twi.h"

#define TWI_STATUS (TWSR & 0xF8)

#define TWI_START        0x08
#define TWI_RESTART      0x10
#define TWI_MT_SLA_ACK   0x18
#define TWI_MT_DATA_ACK  0x28
#define TWI_MR_SLA_ACK   0x40
#define TWI_MR_DATA_ACK  0x50
#define TWI_MR_DATA_NACK 0x58

static uint8_t twi_wait_twint(void)
{
    while(!(TWCR & (1<<TWINT)));
    return TWI_STATUS;
}

void twi_init(void)
{
    TWBR = TWBR_VALUE;
    TWSR = (0<<TWPS1) | (0<<TWPS0); // prescaler 1x
    TWCR = (1<<TWEN);
}

uint8_t twi_start(void)
{
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    return (twi_wait_twint() == TWI_START) ? 0 : 1;
}

uint8_t twi_restart(void)
{
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    return (twi_wait_twint() == TWI_RESTART) ? 0 : 1;
}

uint8_t twi_write_byte(uint8_t data)
{
    uint8_t status;

    TWDR = data;
    TWCR = (1<<TWINT) | (1<<TWEN);
    status = twi_wait_twint();

    // El primer byte tras START/RESTART es SLA+R/W y ACKa con
    // MT_SLA_ACK (0x18); los bytes de datos ACKan con MT_DATA_ACK
    // (0x28) y la lectura con MR_SLA_ACK (0x40).
    return (status == TWI_MT_SLA_ACK
         || status == TWI_MT_DATA_ACK
         || status == TWI_MR_SLA_ACK) ? 0 : 1;
}

uint8_t twi_read_ack(uint8_t *data)
{
    TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN);

    if(twi_wait_twint() != TWI_MR_DATA_ACK)
        return 1;

    *data = TWDR;
    return 0;
}

uint8_t twi_read_nack(uint8_t *data)
{
    TWCR = (1<<TWINT) | (1<<TWEN);

    if(twi_wait_twint() != TWI_MR_DATA_NACK)
        return 1;

    *data = TWDR;
    return 0;
}

void twi_stop(void)
{
    uint16_t guard = 0;

    TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN);

    // Espera a que el hardware complete la condicion STOP; si no,
    // un START inmediatamente posterior puede perderse.
    while((TWCR & (1<<TWSTO)) && ++guard < 10000);
}