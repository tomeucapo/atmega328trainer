#include <stddef.h>
#include <string.h>
#include "lora_proto.h"

uint16_t lora_crc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFF;

    while(len--)
    {
        crc ^= (uint16_t)(*data++) << 8;

        for(uint8_t i = 0; i < 8; i++)
        {
            if(crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}

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
} lora_weather_v1_t;

void lora_build_weather(lora_weather_t *frame, uint8_t node_id, uint16_t seq,
                        int16_t temp_x10, uint16_t hum_x10,
                        uint16_t press_hpa, uint16_t battery_mv)
{
    frame->magic0 = LORA_MAGIC0;
    frame->magic1 = LORA_MAGIC1;
    frame->type = LORA_TYPE_WEATHER;
    frame->version = LORA_VERSION;
    frame->node_id = node_id;
    frame->seq = seq;
    frame->temp_x10 = temp_x10;
    frame->hum_x10 = hum_x10;
    frame->press_hpa = press_hpa;
    frame->battery_mv = battery_mv;
    frame->crc = lora_crc16((const uint8_t *)frame,
                            (uint8_t)offsetof(lora_weather_t, crc));
}

uint8_t lora_parse_weather(const uint8_t *buf, uint8_t len,
                           lora_weather_t *out)
{
    uint16_t rx_crc;
    uint16_t calc_crc;

    if(len < 4)
        return 0;

    if(buf[0] != LORA_MAGIC0 || buf[1] != LORA_MAGIC1)
        return 0;

    if(buf[2] != LORA_TYPE_WEATHER)
        return 0;

    if(buf[3] == LORA_VERSION_LEGACY)
    {
        lora_weather_v1_t legacy;

        if(len != sizeof(legacy))
            return 0;

        memcpy(&legacy, buf, sizeof(legacy));
        rx_crc = legacy.crc;
        calc_crc = lora_crc16(buf, (uint8_t)offsetof(lora_weather_v1_t, crc));
        if(rx_crc != calc_crc)
            return 0;

        out->magic0 = legacy.magic0;
        out->magic1 = legacy.magic1;
        out->type = legacy.type;
        out->version = LORA_VERSION_LEGACY;
        out->node_id = 1; // Los nodos anteriores se consideran el nodo 1.
        out->seq = legacy.seq;
        out->temp_x10 = legacy.temp_x10;
        out->hum_x10 = legacy.hum_x10;
        out->press_hpa = legacy.press_hpa;
        out->battery_mv = legacy.battery_mv;
        out->crc = legacy.crc;
        return 1;
    }

    if((buf[3] != LORA_VERSION_NODE_ID && buf[3] != LORA_VERSION) ||
       len != LORA_WEATHER_SIZE)
        return 0;

    memcpy(out, buf, sizeof(*out));
    rx_crc = out->crc;
    calc_crc = lora_crc16(buf, (uint8_t)offsetof(lora_weather_t, crc));

    return (uint8_t)(rx_crc == calc_crc);
}
