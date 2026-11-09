#ifndef DS18B20_H
#define DS18B20_H
#define DS18B20_ERROR_TEMP (-127.0f)

#define DS18B20_CONFIG 0x1F  // resolucion 9-bit (0.5 C, ~94 ms)
#define DS18B20_CONV_MS 100

void ds18b20_init(void);
float ds18b20_get_temp(void);
uint8_t ds18b20_get_temp_debug(float *temp, uint8_t *scratchpad);
uint8_t ds18b20_scratchpad_crc_ok(const uint8_t *scratchpad);

#endif
