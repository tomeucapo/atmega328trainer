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

float ds18b20_get_temp(void)
{
    uint8_t low, high;
    uint8_t resolution;
    uint16_t temp_raw;
    int16_t raw;

    if(onewire_reset() != 0)
        return DS18B20_ERROR_TEMP;

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0x44); // Convert T

    _delay_ms(DS18B20_CONV_MS);

    if(onewire_reset() != 0)
        return DS18B20_ERROR_TEMP;

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0xBE); // Read scratchpad

    // Solo se necesitan los bytes de temperatura; la resolucion se fija
    // en ds18b20_init() y determina cuantos bits bajos son indefinidos.
    low = onewire_read_byte();
    high = onewire_read_byte();
    temp_raw = ((uint16_t)high << 8) | low;
    resolution = (DS18B20_CONFIG >> 5) & 0x03;
    temp_raw &= (uint16_t)~((1U << (3 - resolution)) - 1U);
    raw = (int16_t)temp_raw;

    return raw * 0.0625f;
}
