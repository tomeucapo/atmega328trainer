#include "onewrite.h"

static void onewire_low(void)
{
    DS18B20_PORT &= ~(1<<DS18B20_BIT);
    DS18B20_DDR |= (1<<DS18B20_BIT);
}

static void onewire_release(void)
{
    DS18B20_DDR &= ~(1<<DS18B20_BIT);
    DS18B20_PORT |= (1<<DS18B20_BIT);
}

static uint8_t onewire_read_pin(void)
{
    return (DS18B20_PIN & (1<<DS18B20_BIT)) ? 1 : 0;
}

static uint8_t onewire_reset(void)
{
    uint8_t presence;

    cli();
    onewire_low();
    _delay_us(480);
    onewire_release();
    _delay_us(60);
    presence = onewire_read_pin();
    _delay_us(420);
    sei();

    return presence; // 0 = dispositivo presente
}

static void onewire_write_bit(uint8_t bit)
{
    cli();
    if(bit)
    {
        onewire_low();
        _delay_us(6);
        onewire_release();
        _delay_us(64);
    }
    else
    {
        onewire_low();
        _delay_us(60);
        onewire_release();
        _delay_us(10);
    }
    sei();
}

static uint8_t onewire_read_bit(void)
{
    uint8_t bit;

    cli();
    onewire_low();
    _delay_us(6);
    onewire_release();
    _delay_us(9);
    bit = onewire_read_pin();
    _delay_us(55);
    sei();

    return bit;
}

static void onewire_write_byte(uint8_t byte)
{
    for(uint8_t i = 0; i < 8; i++)
    {
        onewire_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;

    for(uint8_t i = 0; i < 8; i++)
    {
        data >>= 1;
        if(onewire_read_bit())
            data |= 0x80;
    }

    return data;
}
