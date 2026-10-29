#ifndef SX127X_H
#define SX127X_H

#include <stdint.h>

// ======================================================
// SX1276/77/78/79 (RFM95W / Ra-02) en modo LoRa punto a punto.
// SPI: PB3=MOSI, PB4=MISO, PB5=SCK. CS y RESET se definen aqui.
// El modulo debe alimentarse a 3.3 V (el ATmega de 5 V necesita
// level shifting en las lineas SPI o un modulo con regulador).
// ======================================================

#ifndef SX127X_FREQ_HZ
#define SX127X_FREQ_HZ 868000000UL   // RFM95W UE (Ra-02: 433000000UL)
#endif

// CS en PD2, RESET en PD3 (libres en el header J7 si no se usa
// la IOExtension). CS debe tener pull-up externo a 3.3 V.
#define SX127X_CS_DDR   DDRD
#define SX127X_CS_PORT  PORTD
#define SX127X_CS_BIT   PD2

#define SX127X_RST_DDR  DDRD
#define SX127X_RST_PORT PORTD
#define SX127X_RST_BIT  PD3

#define SX127X_MAX_PAYLOAD 64

// Devuelve 1 si el chip responde (RegVersion valido) y queda en RX.
uint8_t sx127x_init(void);

// Deja el radio en recepcion continua.
void sx127x_set_rx(void);

// Duerme el radio (bajo consumo entre transmisiones del nodo).
void sx127x_sleep(void);

// Transmite un paquete (max. SX127X_MAX_PAYLOAD) y vuelve a RX.
// Devuelve 1 si se completo el TxDone.
uint8_t sx127x_send(const uint8_t *data, uint8_t len);

// Saca un paquete si ya llego. Devuelve 1 y actualiza *len.
uint8_t sx127x_poll_rx(uint8_t *buf, uint8_t *len);

#endif
