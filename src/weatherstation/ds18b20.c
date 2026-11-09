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

// CRC-8 Dallas/Maxim del scratchpad (polinomio x^8+x^5+x^4+1).
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

    for(uint8_t i = 0; i < sizeof(scratchpad); i++)
        scratchpad[i] = onewire_read_byte();

    // Descarta datos corruptos o una linea que este permanentemente baja.
    if(ds18b20_crc8(scratchpad, 8) != scratchpad[8] ||
       (scratchpad[4] & 0x1F) != 0x1F)
        return DS18B20_ERROR_TEMP;

    // Combina en unsigned para evitar desplazar un entero signed fuera de rango.
    temp_raw = ((uint16_t)scratchpad[1] << 8) | scratchpad[0];
    resolution = (scratchpad[4] >> 5) & 0x03;
    temp_raw &= (uint16_t)~((1U << (3 - resolution)) - 1U);
    raw = (int16_t)temp_raw;

    return raw * 0.0625f;
}
