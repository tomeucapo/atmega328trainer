#include <stdint.h>
#include <util/delay.h>
#include "onewire.h"
#include "ds18b20.h"

// ======================================================
// DS18B20 TEMPERATURE SENSOR
// PB2 = 1-WIRE
// ======================================================

// PB2 tambien es SS del SPI. Un nivel bajo en SS con SPI maestro activo
// limpia MSTR; se suspende el periferico durante las operaciones 1-Wire.
static uint8_t ds18b20_spi_suspend(void)
{
    if(SPCR & (1 << SPE))
    {
        SPCR &= (uint8_t)~(1 << SPE);
        return 1;
    }

    return 0;
}

static void ds18b20_spi_resume(uint8_t was_enabled)
{
    if(was_enabled)
        SPCR |= (1 << MSTR) | (1 << SPE);
}

void ds18b20_init(void)
{
    uint8_t spi_was_enabled = ds18b20_spi_suspend();

    DS18B20_DDR &= ~(1<<DS18B20_BIT);
    DS18B20_PORT |= (1<<DS18B20_BIT);

    // Fija resolucion 9-bit en el scratchpad (RAM, no persiste tras reset).
    if(onewire_reset() == 0)
    {
        onewire_write_byte(0xCC); // Skip ROM
        onewire_write_byte(0x4E); // Write scratchpad
        onewire_write_byte(0x00); // TH
        onewire_write_byte(0x00); // TL
        onewire_write_byte(DS18B20_CONFIG);
        onewire_reset(); // fin del Write scratchpad
    }

    ds18b20_spi_resume(spi_was_enabled);
}

float ds18b20_get_temp(void)
{
    uint8_t low, high;
    int16_t raw;
    uint8_t spi_was_enabled = ds18b20_spi_suspend();

    if(onewire_reset() != 0)
    {
        ds18b20_spi_resume(spi_was_enabled);
        return DS18B20_ERROR_TEMP;
    }

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0x44); // Convert T

    _delay_ms(DS18B20_CONV_MS);

    if(onewire_reset() != 0)
    {
        ds18b20_spi_resume(spi_was_enabled);
        return DS18B20_ERROR_TEMP;
    }

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0xBE); // Read scratchpad

    low = onewire_read_byte();
    high = onewire_read_byte();

    raw = (high << 8) | low;

    float temp = raw * 0.0625f;
    ds18b20_spi_resume(spi_was_enabled);
    return temp;
}
