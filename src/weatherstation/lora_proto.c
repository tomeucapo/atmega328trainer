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

void lora_build_weather(lora_weather_t *frame, uint16_t seq,
                        int16_t temp_x10, uint16_t hum_x10,
                        uint16_t press_hpa, uint16_t battery_mv)
{
    frame->magic0 = LORA_MAGIC0;
    frame->magic1 = LORA_MAGIC1;
    frame->type = LORA_TYPE_WEATHER;
    frame->version = LORA_VERSION;
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

    if(len != LORA_WEATHER_SIZE)
        return 0;

    memcpy(out, buf, sizeof(*out));

    if(out->magic0 != LORA_MAGIC0 || out->magic1 != LORA_MAGIC1)
        return 0;

    if(out->type != LORA_TYPE_WEATHER || out->version != LORA_VERSION)
        return 0;

    rx_crc = out->crc;
    calc_crc = lora_crc16(buf, (uint8_t)offsetof(lora_weather_t, crc));

    return (uint8_t)(rx_crc == calc_crc);
}
