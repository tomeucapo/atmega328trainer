#ifndef PREDICT_H
#define PREDICT_H

#include <stdint.h>

// Muestra compacta (10 B) para no gastar SRAM: los valores se
// guardan escalados x10 en enteros en lugar de float.
typedef struct {
    int16_t temp_x10;     // decimas de grado C
    uint16_t hum_x10;     // decimas de %HR
    uint16_t press_hpa;   // hPa enteros
    int16_t wind_x10;     // decimas de km/h
    uint16_t rain_mm_x10; // decimas de mm
} pred_sample_t;

void pred_init(void);
void pred_add_sample(float temp, float hum, float press, float wind, float rain_mm);

// Escribe en 'out' (RAM) una prediccion de texto copiada desde flash.
void pred_forecast(char *out, uint16_t len);
float pred_dewpoint(float temp, float hum);
float pred_heat_index(float temp, float hum);

#endif
