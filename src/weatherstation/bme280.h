#ifndef BME280_H
#define BME280_H

#include <stdint.h>

#define BME280_I2C_ADDR 0x76

typedef struct {
    float temperature;
    float humidity;
    float pressure;
} bme280_data_t;

uint8_t bme280_init(void);
uint8_t bme280_read(bme280_data_t *data);

// Modo de bajo consumo para el nodo remoto.
void bme280_sleep(void);
uint8_t bme280_forced_read(bme280_data_t *data);

#endif
