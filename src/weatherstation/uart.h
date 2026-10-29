#ifndef UART_H
#define UART_H

#include <stdint.h>

#define UART_BAUD 9600
#define UART_RX_BUF_SIZE 64    // potencia de 2

void uart_init(void);
void uart_tx_char(char c);
void uart_tx_string(const char *s);

// Igual que uart_tx_string() pero el texto vive en flash (PSTR()):
// imprescindible para no llenar la SRAM con literales.
void uart_tx_string_P(const char *s);

// Numero de bytes pendientes de leer en el buffer RX.
uint8_t uart_rx_available(void);

// Devuelve y consume el siguiente byte del buffer RX.
uint8_t uart_rx_read(void);

void uart_tx_decimal(int16_t v);
void uart_tx_uint32(uint32_t v);
void uart_tx_time(uint32_t seconds);

#endif
