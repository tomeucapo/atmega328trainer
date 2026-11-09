#include <stdint.h>
#include <util/delay.h>
#include "onewire.h" 
#include "ds18b20.h"

// ======================================================
// DS18B20 TEMPERATURE SENSOR
// PB2 = 1-WIRE
// ======================================================


void ds18b20_init(void)
{
    DS18B20_DDR &= ~(1<<DS18B20_BIT);
    DS18B20_PORT |= (1<<DS18B20_BIT);

    // fijar resolucion 9-bit en el scratchpad (RAM, no persiste tras reset)
    if(onewire_reset() == 0)
    {
        onewire_write_byte(0xCC); // Skip ROM
        onewire_write_byte(0x4E); // Write scratchpad
        onewire_write_byte(0x00); // TH
        onewire_write_byte(0x00); // TL
        onewire_write_byte(DS18B20_CONFIG);
        onewire_reset(); // fin del Write scratchpad
    }
}

static uint8_t ds18b20_start_read(void)
{
    if(onewire_reset() != 0)
        return 0;

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0x44); // Convert T
    _delay_ms(DS18B20_CONV_MS);

    if(onewire_reset() != 0)
        return 0;

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0xBE); // Read scratchpad
    return 1;
}

static float ds18b20_decode_temp(uint8_t low, uint8_t high)
{
    uint8_t resolution = (DS18B20_CONFIG >> 5) & 0x03;
    uint16_t temp_raw = ((uint16_t)high << 8) | low;
    int16_t raw;

    temp_raw &= (uint16_t)~((1U << (3 - resolution)) - 1U);
    raw = (int16_t)temp_raw;
    return raw * 0.0625f;
}

static uint8_t ds18b20_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;

    while(len--)
    {
        uint8_t value = *data++;

        for(uint8_t i = 0; i < 8; i++)
        {
            uint8_t mix = (crc ^ value) & 0x01;
            crc >>= 1;
            if(mix)
                crc ^= 0x8C;
            value >>= 1;
        }
    }

    return crc;
}

float ds18b20_get_temp(void)
{
    uint8_t scratchpad[9];
    float temp;

    if(!ds18b20_get_temp_debug(&temp, scratchpad) ||
       !ds18b20_scratchpad_crc_ok(scratchpad))
        return DS18B20_ERROR_TEMP;

    return temp;
}

uint8_t ds18b20_get_temp_debug(float *temp, uint8_t *scratchpad)
{
    if(!ds18b20_start_read())
        return 0;

    for(uint8_t i = 0; i < 9; i++)
        scratchpad[i] = onewire_read_byte();

    *temp = ds18b20_decode_temp(scratchpad[0], scratchpad[1]);
    return 1;
}

uint8_t ds18b20_scratchpad_crc_ok(const uint8_t *scratchpad)
{
    return (uint8_t)(ds18b20_crc8(scratchpad, 8) == scratchpad[8]);
}
