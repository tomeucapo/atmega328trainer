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

    low = onewire_read_byte();
    high = onewire_read_byte();

    raw = (high << 8) | low;

    return raw * 0.0625f;
}