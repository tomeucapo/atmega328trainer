#ifndef LORA_PROTO_H
#define LORA_PROTO_H

#include <stdint.h>

// ======================================================
// Protocolo minimo punto a punto para el nodo remoto.
// Trama fija de 16 B con CRC-16/CCITT.
// ======================================================

#define LORA_MAGIC0  'W'
#define LORA_MAGIC1  'X'
#define LORA_VERSION 1
#define LORA_TYPE_WEATHER 1

typedef struct __attribute__((packed)) {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  type;
    uint8_t  version;
    uint16_t seq;
    int16_t  temp_x10;
    uint16_t hum_x10;
    uint16_t press_hpa;
    uint16_t battery_mv;
    uint16_t crc;
} lora_weather_t;

#define LORA_WEATHER_SIZE ((uint8_t)sizeof(lora_weather_t))

uint16_t lora_crc16(const uint8_t *data, uint8_t len);

// Rellena la trama (incluido el CRC).
void lora_build_weather(lora_weather_t *frame, uint16_t seq,
                        int16_t temp_x10, uint16_t hum_x10,
                        uint16_t press_hpa, uint16_t battery_mv);

// Valida magic/version/tipo/CRC y copia la trama.
uint8_t lora_parse_weather(const uint8_t *buf, uint8_t len,
                           lora_weather_t *out);

#endif
