#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>

// ======================================================
// OLED SSD1306 128x64 I2C
// PC4=SDA, PC5=SCL (bus TWI compartido con MCP23017)
// ======================================================

#define SSD1306_I2C_ADDR 0x3C   // SA0 = VCC (0x3D si SA0 = GND)

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES  (SSD1306_HEIGHT / 8)

// Requiere twi_init() previo (lo hace mcp23017_init()).
void ssd1306_init(void);
void ssd1306_clear(void);

// Dibujo en el buffer en RAM (fuente 5x7, un char = 6 px de ancho)
void ssd1306_draw_char(uint8_t col, uint8_t page, char c);
void ssd1306_draw_string(uint8_t page, uint8_t col, const char *s);
void ssd1306_draw_string_centered(uint8_t page, const char *s);

// Envia a la pantalla solo las paginas cuyo contenido cambio.
void ssd1306_update(void);

#endif