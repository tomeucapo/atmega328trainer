#ifndef MCP23017_H
#define MCP23017_H

#include <stdint.h>

#define MCP23017_I2C_ADDR 0x20

#define MCP23017_PORTA 0
#define MCP23017_PORTB 1

// Registros (modo BANK=0, IOCON=0x00 por defecto)
#define MCP23017_IODIRA   0x00
#define MCP23017_IODIRB   0x01
#define MCP23017_IPOLA    0x02
#define MCP23017_IPOLB    0x03
#define MCP23017_GPINTENA 0x04
#define MCP23017_GPINTENB 0x05
#define MCP23017_DEFVALA  0x06
#define MCP23017_DEFVALB  0x07
#define MCP23017_INTCONA  0x08
#define MCP23017_INTCONB  0x09
#define MCP23017_IOCON    0x0A
#define MCP23017_GPPUA    0x0C
#define MCP23017_GPPUB    0x0D
#define MCP23017_INTFA    0x0E
#define MCP23017_INTFB    0x0F
#define MCP23017_INTCAPA  0x10
#define MCP23017_INTCAPB  0x11
#define MCP23017_GPIOA    0x12
#define MCP23017_GPIOB    0x13
#define MCP23017_OLATA    0x14
#define MCP23017_OLATB    0x15

#define MCP23017_OUTPUT 0
#define MCP23017_INPUT  1

void mcp23017_init(void);
void mcp23017_direction(uint8_t port, uint8_t mask); // 1 = entrada, 0 = salida
void mcp23017_pullup(uint8_t port, uint8_t mask);
void mcp23017_write_port(uint8_t port, uint8_t value);
uint8_t mcp23017_read_port(uint8_t port);
void mcp23017_write_pin(uint8_t port, uint8_t pin, uint8_t value);
uint8_t mcp23017_read_pin(uint8_t port, uint8_t pin);
void mcp23017_write_reg(uint8_t reg, uint8_t value);
uint8_t mcp23017_read_reg(uint8_t reg);

#endif