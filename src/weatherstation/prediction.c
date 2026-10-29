#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <math.h>
#include <string.h>
#include <avr/pgmspace.h>
#include "prediction.h"

#define PRED_HIST 12

static pred_sample_t hist[PRED_HIST];
static uint8_t hist_head = 0;
static uint8_t hist_count = 0;

static int16_t scaled_i16(float v)
{
    float s = v * 10.0f;

    if(s >= 0.0f)
        return (int16_t)(s + 0.5f);

    return (int16_t)(s - 0.5f);
}

static uint16_t scaled_u16(float v)
{
    if(v <= 0.0f)
        return 0;

    if(v >= 6553.5f)
        return 65535;

    return (uint16_t)(v * 10.0f + 0.5f);
}

static void copy_P(char *out, uint16_t len, const char *src)
{
    if(len == 0)
        return;

    strncpy_P(out, src, len);
    out[len - 1] = '\0';
}

void pred_init(void)
{
    hist_head = 0;
    hist_count = 0;
    memset(hist, 0, sizeof(hist));
}

void pred_add_sample(float temp, float hum, float press, float wind, float rain_mm)
{
    uint8_t idx = hist_head;

    hist[idx].temp_x10 = scaled_i16(temp);
    hist[idx].hum_x10 = scaled_u16(hum);
    hist[idx].press_hpa = scaled_u16(press);
    hist[idx].wind_x10 = scaled_i16(wind);
    hist[idx].rain_mm_x10 = scaled_u16(rain_mm);

    hist_head = (hist_head + 1) % PRED_HIST;
    if(hist_count < PRED_HIST)
        hist_count++;
}

float pred_dewpoint(float temp, float hum)
{
    if(hum <= 0.0f) return -999.0f;
    if(hum > 100.0f) hum = 100.0f;
    float a = 17.27f;
    float b = 237.7f;
    float gamma = (a * temp) / (b + temp) + logf(hum / 100.0f);
    float dp = (b * gamma) / (a - gamma);
    return dp;
}

float pred_heat_index(float temp, float hum)
{
    // simple approximation for > 27C
    if(temp < 27.0f) return temp;
    float T = temp;
    float R = hum;
    float HI = -8.78469475556f + 1.61139411f*T + 2.33854883889f*R 
             - 0.14611605f*T*R - 0.012308094f*T*T - 0.0164248277778f*R*R 
             + 0.002211732f*T*T*R + 0.00072546f*T*R*R 
             - 0.000003582f*T*T*R*R;
    return HI;
}

void pred_forecast(char *out, uint16_t len)
{
    if(hist_count < 3)
    {
        copy_P(out, len, PSTR("PRED: INSUFICIENTES DATOS"));
        return;
    }
    
    // get last and older samples
    uint8_t last = (hist_head + PRED_HIST - 1) % PRED_HIST;
    uint8_t prev2 = (hist_head + PRED_HIST - 3) % PRED_HIST;
    
    float press_now = hist[last].press_hpa;
    float press_p2 = hist[prev2].press_hpa;
    
    float dpress_total = press_now - press_p2;
    
    float hum_now = hist[last].hum_x10 / 10.0f;
    float wind_now = hist[last].wind_x10 / 10.0f;
    float dp_now = pred_dewpoint(hist[last].temp_x10 / 10.0f, hum_now);
    float temp_now = hist[last].temp_x10 / 10.0f;
    
    // Zambretti-like simplified logic
    if(press_now < 1005.0f && dpress_total < -1.0f && hum_now > 80.0f)
    {
        copy_P(out, len, PSTR("PRED: LLUVIA PROBABLE/INMINENTE"));
        return;
    }
    if(dpress_total < -1.5f && hum_now > 75.0f)
    {
        copy_P(out, len, PSTR("PRED: POSIBLE LLUVIA (BAJADA PRESION)"));
        return;
    }
    if(dpress_total > 1.0f && hum_now < 70.0f && wind_now < 15.0f)
    {
        copy_P(out, len, PSTR("PRED: MEJORANDO / SOLEADO"));
        return;
    }
    if(press_now > 1023.0f && dpress_total >= -0.2f && hum_now < 60.0f)
    {
        copy_P(out, len, PSTR("PRED: ESTABLE / ANTICICLON"));
        return;
    }
    if(hum_now > 90.0f && temp_now - dp_now < 2.0f)
    {
        copy_P(out, len, PSTR("PRED: NIEBLA/ROCIO PROBABLE"));
        return;
    }
    if(wind_now > 25.0f && dpress_total < -0.5f)
    {
        copy_P(out, len, PSTR("PRED: VIENTOS FUERTES + INESTABLE"));
        return;
    }
    if(dpress_total > 0.5f)
    {
        copy_P(out, len, PSTR("PRED: TENDENCIA A MEJORAR"));
        return;
    }
    if(dpress_total < -0.5f)
    {
        copy_P(out, len, PSTR("PRED: TENDENCIA A EMPEORAR"));
        return;
    }
    
    copy_P(out, len, PSTR("PRED: TIEMPO ESTABLE"));
}
