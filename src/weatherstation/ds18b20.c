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

float ds18b20_get_temp(void)
{
    uint8_t low, high;

    if(!ds18b20_start_read())
        return DS18B20_ERROR_TEMP;

    low = onewire_read_byte();
    high = onewire_read_byte();
    return ds18b20_decode_temp(low, high);
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
