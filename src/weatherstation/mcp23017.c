#include <avr/io.h>
#include "twi.h"
#include "mcp23017.h"

void mcp23017_write_reg(uint8_t reg, uint8_t value)
{
    twi_start();
    twi_write_byte(MCP23017_I2C_ADDR << 1); // modo escritura
    twi_write_byte(reg);
    twi_write_byte(value);
    twi_stop();
}

uint8_t mcp23017_read_reg(uint8_t reg)
{
    uint8_t value = 0;

    twi_start();
    twi_write_byte(MCP23017_I2C_ADDR << 1); // modo escritura
    twi_write_byte(reg);

    twi_restart();
    twi_write_byte((MCP23017_I2C_ADDR << 1) | 0x01); // modo lectura
    twi_read_nack(&value);

    twi_stop();

    return value;
}

uint8_t mcp23017_detect(void)
{
    uint8_t found = 0;

    if(twi_start() == 0)
    {
        if(twi_write_byte(MCP23017_I2C_ADDR << 1) == 0)
            found = 1;
    }

    twi_stop();

    return found;
}

void mcp23017_init(void)
{
    twi_init();

    // BANK=0 (mapa de registros secuencial, ver mcp23017.h)
    mcp23017_write_reg(MCP23017_IOCON, 0x00);

    // GPA7 es entrada con pulsador a masa (no tocar como salida),
    // GPA0..GPA6 salidas a 0, GPB todo salidas a 0. Se configura
    // IODIRA una sola vez para no pisar el sentido de GPA7.
    mcp23017_write_reg(MCP23017_IODIRA, 0x80);
    mcp23017_write_reg(MCP23017_IODIRB, 0x00);
    mcp23017_write_reg(MCP23017_OLATA, 0x00);
    mcp23017_write_reg(MCP23017_OLATB, 0x00);
}

void mcp23017_direction(uint8_t port, uint8_t mask)
{
    uint8_t reg = (port == MCP23017_PORTA) ? MCP23017_IODIRA : MCP23017_IODIRB;
    mcp23017_write_reg(reg, mask);
}

void mcp23017_pullup(uint8_t port, uint8_t mask)
{
    uint8_t reg = (port == MCP23017_PORTA) ? MCP23017_GPPUA : MCP23017_GPPUB;
    mcp23017_write_reg(reg, mask);
}

void mcp23017_write_port(uint8_t port, uint8_t value)
{
    uint8_t reg = (port == MCP23017_PORTA) ? MCP23017_OLATA : MCP23017_OLATB;
    mcp23017_write_reg(reg, value);
}

uint8_t mcp23017_read_port(uint8_t port)
{
    uint8_t reg = (port == MCP23017_PORTA) ? MCP23017_GPIOA : MCP23017_GPIOB;
    return mcp23017_read_reg(reg);
}

void mcp23017_write_pin(uint8_t port, uint8_t pin, uint8_t value)
{
    uint8_t reg = (port == MCP23017_PORTA) ? MCP23017_OLATA : MCP23017_OLATB;
    uint8_t latch = mcp23017_read_reg(reg);
    uint8_t mask = (1 << pin);

    if(value)
        latch |= mask;
    else
        latch &= ~mask;

    mcp23017_write_reg(reg, latch);
}

uint8_t mcp23017_read_pin(uint8_t port, uint8_t pin)
{
    return (mcp23017_read_port(port) >> pin) & 0x01;
}