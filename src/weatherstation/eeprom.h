#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

// ======================================================
// EEPROM I2C 24AA515 (512 kbit / 64 KB) con buffer circular
// de lecturas periodicas. Comparte el bus TWI con el
// MCP23017 (0x20) y el SSD1306 (0x3C).
// ======================================================

// Lee la cabecera y recupera el estado del buffer circular.
// Si no hay EEPROM en el bus, las demas funciones no hacen nada.
void eeprom_init(void);

// Guarda una lectura periodica.
void eeprom_log(uint32_t uptime_s, float wind_kmh, float temp_c, uint32_t rain_tips);

// Vuelca por UART todas las lecturas, de la mas antigua a la
// mas nueva. Bloquea mientras dura el envio.
void eeprom_dump(void);

// Numero de lecturas almacenadas.
uint32_t eeprom_count(void);

#endif
