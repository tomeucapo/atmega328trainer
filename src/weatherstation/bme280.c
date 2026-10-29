#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <math.h>
#include <stdint.h>
#include "twi.h"
#include "bme280.h"

#define BME280_REG_CALIB00 0x88
#define BME280_REG_CALIB25 0xA1
#define BME280_REG_CALIB26 0xE1
#define BME280_REG_ID 0xD0
#define BME280_REG_RESET 0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS 0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_PRESS_MSB 0xF7

// Calibration data
static uint16_t dig_T1;
static int16_t dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t dig_H1, dig_H3;
static int16_t dig_H2, dig_H4, dig_H5;
static int8_t dig_H6;

static int32_t t_fine;

static uint8_t bme280_read_reg(uint8_t reg, uint8_t *data)
{
    if(twi_start()) return 1;
    if(twi_write_byte((BME280_I2C_ADDR << 1) | 0)) { twi_stop(); return 1; }
    if(twi_write_byte(reg)) { twi_stop(); return 1; }
    if(twi_restart()) { twi_stop(); return 1; }
    if(twi_write_byte((BME280_I2C_ADDR << 1) | 1)) { twi_stop(); return 1; }
    if(twi_read_nack(data)) { twi_stop(); return 1; }
    twi_stop();
    return 0;
}

static uint8_t bme280_read_regs(uint8_t reg, uint8_t *data, uint8_t len)
{
    if(twi_start()) return 1;
    if(twi_write_byte((BME280_I2C_ADDR << 1) | 0)) { twi_stop(); return 1; }
    if(twi_write_byte(reg)) { twi_stop(); return 1; }
    if(twi_restart()) { twi_stop(); return 1; }
    if(twi_write_byte((BME280_I2C_ADDR << 1) | 1)) { twi_stop(); return 1; }
    for(uint8_t i = 0; i < len; i++)
    {
        if(i == len-1)
        {
            if(twi_read_nack(&data[i])) { twi_stop(); return 1; }
        }
        else
        {
            if(twi_read_ack(&data[i])) { twi_stop(); return 1; }
        }
    }
    twi_stop();
    return 0;
}

static uint8_t bme280_write_reg(uint8_t reg, uint8_t val)
{
    if(twi_start()) return 1;
    if(twi_write_byte((BME280_I2C_ADDR << 1) | 0)) { twi_stop(); return 1; }
    if(twi_write_byte(reg)) { twi_stop(); return 1; }
    if(twi_write_byte(val)) { twi_stop(); return 1; }
    twi_stop();
    return 0;
}

static void bme280_read_calibration(void)
{
    uint8_t cal[26];
    uint8_t h1;
    
    bme280_read_regs(BME280_REG_CALIB00, cal, 24);
    
    dig_T1 = (uint16_t)(cal[0] | (cal[1] << 8));
    dig_T2 = (int16_t)(cal[2] | (cal[3] << 8));
    dig_T3 = (int16_t)(cal[4] | (cal[5] << 8));
    dig_P1 = (uint16_t)(cal[6] | (cal[7] << 8));
    dig_P2 = (int16_t)(cal[8] | (cal[9] << 8));
    dig_P3 = (int16_t)(cal[10] | (cal[11] << 8));
    dig_P4 = (int16_t)(cal[12] | (cal[13] << 8));
    dig_P5 = (int16_t)(cal[14] | (cal[15] << 8));
    dig_P6 = (int16_t)(cal[16] | (cal[17] << 8));
    dig_P7 = (int16_t)(cal[18] | (cal[19] << 8));
    dig_P8 = (int16_t)(cal[20] | (cal[21] << 8));
    dig_P9 = (int16_t)(cal[22] | (cal[23] << 8));
    
    bme280_read_reg(BME280_REG_CALIB25, &h1);
    dig_H1 = h1;
    
    bme280_read_regs(BME280_REG_CALIB26, cal, 7);
    dig_H2 = (int16_t)(cal[0] | (cal[1] << 8));
    dig_H3 = cal[2];
    dig_H4 = (int16_t)((cal[3] << 4) | (cal[4] & 0x0F));
    dig_H5 = (int16_t)((cal[5] << 4) | (cal[4] >> 4));
    dig_H6 = (int8_t)cal[6];
}

static int32_t bme280_compensate_temp(int32_t adc_T)
{
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

static uint32_t bme280_compensate_press(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    if(var1 == 0) return 0;
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (uint32_t)p;
}

static uint32_t bme280_compensate_hum(int32_t adc_H)
{
    int32_t v_x1_u32r;
    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * 
                  (((((((v_x1_u32r * ((int32_t)dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * 
                  ((int32_t)dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    return (uint32_t)(v_x1_u32r >> 12);
}

uint8_t bme280_init(void)
{
    uint8_t id = 0;
    if(bme280_read_reg(BME280_REG_ID, &id)) return 0;
    if(id != 0x60) return 0;
    
    bme280_write_reg(BME280_REG_RESET, 0xB6);
    _delay_ms(10);
    
    bme280_read_calibration();
    
    // humidity oversampling x1
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01);
    // temp x1, press x1, normal mode
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x27);
    // config
    bme280_write_reg(BME280_REG_CONFIG, 0xA0);
    
    return 1;
}

// Deja el sensor en sleep (modo normal -> 0). Util para nodos
// alimentados con bateria.
void bme280_sleep(void)
{
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x00);
}

// Dispara una conversion en modo forced y espera a que termine.
uint8_t bme280_forced_read(bme280_data_t *data)
{
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01);
    // temp x1, press x1, hum x1, modo forced (01)
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x25);
    _delay_ms(12);

    return bme280_read(data);
}

uint8_t bme280_read(bme280_data_t *data)
{
    uint8_t buf[8];
    if(bme280_read_regs(BME280_REG_PRESS_MSB, buf, 8)) return 1;
    
    int32_t adc_P = (int32_t)((buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4));
    int32_t adc_T = (int32_t)((buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4));
    int32_t adc_H = (int32_t)((buf[6] << 8) | buf[7]);
    
    int32_t T = bme280_compensate_temp(adc_T);
    uint32_t P = bme280_compensate_press(adc_P);
    uint32_t H = bme280_compensate_hum(adc_H);
    
    data->temperature = T / 100.0f;
    data->pressure = P / 256.0f / 100.0f; // hPa
    data->humidity = H / 1024.0f;
    
    return 0;
}
