#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include "twi.h"
#include "uart.h"
#include "eeprom.h"

// ======================================================
// EEPROM I2C 24LC64 (64 kbit / 8 KB)
//
// El control byte es 1010 A2 A1 A0 R/W: con A2=VCC, A1=A0=GND
// responde en 0x54 (0x50 + bit A2 = 4). La direccion de palabra
// es de 2 bytes (A12..A0, los bits altos no importan) y la pagina
// de escritura de 32 B.
//
// Mapa:
//   pagina 0 (64 B)  cabecera, 8 copias de 8 B (wear-leveling)
//   offset 64..      registros en buffer circular
//
// Registro v2 (24 B):
//   [0..3]  uptime en segundos        (uint32 LE)
//   [4..5]  velocidad * 10 (km/h)     (uint16 LE)
//   [6..7]  temperatura * 10 (C)      (int16  LE)
//   [8..11] contador de lluvia        (uint32 LE)
//   [12..13] humedad * 10 (%RH)       (uint16 LE)
//   [14..17] presion * 10 (hPa)       (uint32 LE)
//   [18..23] reservado
// ======================================================

#define EEPROM_BASE_ADDR 0x54  // A2=1, A1=0, A0=0 (1010 A2 A1 A0)
#define EEPROM_SIZE      8192UL

#define EEPROM_HDR_PAGE  64
#define EEPROM_HDR_SLOTS 8
#define EEPROM_HDR_MAGIC 0x5A16

#define EEPROM_REC_SIZE  24
#define EEPROM_REC_MAX   ((EEPROM_SIZE - EEPROM_HDR_PAGE) / EEPROM_REC_SIZE)

// Diagnostico por UART durante la inicializacion. Ponlo a 0 cuando
// ya no lo necesites.
#define EEPROM_DEBUG 1

static uint8_t  eeprom_present = 0;
static uint8_t  eeprom_wr_fail = 0;
static uint8_t  hdr_slot = 0;
static uint32_t rec_count = 0;
static uint8_t  eeprom_slave_addr = EEPROM_BASE_ADDR;

static uint8_t eeprom_slave(uint16_t addr)
{
    (void)addr;
    return eeprom_slave_addr;
}

// El chip no responde durante los hasta 5 ms de programacion;
// se sondea el ACK hasta que vuelve a estar listo.
static void eeprom_wait_ready(uint8_t slave_w)
{
    uint16_t guard = 0;

    for(;;)
    {
        twi_start();

        if(twi_write_byte(slave_w) == 0)
        {
            twi_stop();
            return;
        }

        twi_stop();

        if(++guard > 20000)
            return;
    }
}

static void eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t slave = eeprom_slave(addr);

    twi_start();
    twi_write_byte((uint8_t)((slave << 1) | 0));
    twi_write_byte((uint8_t)((addr >> 8) & 0x7F));
    twi_write_byte((uint8_t)(addr & 0xFF));

    for(uint8_t i = 0; i < len; i++)
        twi_write_byte(buf[i]);

    twi_stop();

    eeprom_wait_ready((uint8_t)((slave << 1) | 0));
}

static void eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    uint8_t slave = eeprom_slave(addr);

    twi_start();
    twi_write_byte((uint8_t)((slave << 1) | 0));
    twi_write_byte((uint8_t)((addr >> 8) & 0x7F));
    twi_write_byte((uint8_t)(addr & 0xFF));

    twi_restart();
    twi_write_byte((uint8_t)((slave << 1) | 1));

    while(len--)
    {
        if(len)
            twi_read_ack(buf);
        else
            twi_read_nack(buf);

        buf++;
    }

    twi_stop();
}

// Relee lo escrito y lo compara: detecta chips sin escribir en
// el array (por ejemplo WP activo) donde el ACK si responde.
static uint8_t eeprom_verify(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t tmp[EEPROM_REC_SIZE];

    if(len > EEPROM_REC_SIZE) len = EEPROM_REC_SIZE;
    eeprom_read(addr, tmp, len);

    return (uint8_t)(memcmp(tmp, buf, len) != 0);
}

static uint8_t eeprom_ack(uint8_t addr)
{
    uint8_t nack;

    twi_start();
    nack = twi_write_byte((uint8_t)(addr << 1));
    twi_stop();

    return (uint8_t)(nack == 0);
}

static void eeprom_write_hdr(uint8_t slot, uint32_t count)
{
    uint8_t buf[8];

    buf[0] = (uint8_t)(EEPROM_HDR_MAGIC);
    buf[1] = (uint8_t)(EEPROM_HDR_MAGIC >> 8);
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = (uint8_t)(count);
    buf[5] = (uint8_t)(count >> 8);
    buf[6] = (uint8_t)(count >> 16);
    buf[7] = (uint8_t)(count >> 24);

    eeprom_write((uint16_t)(slot * 8), buf, 8);
}

#if EEPROM_DEBUG
static void eeprom_tx_hex(const uint8_t *b, uint8_t n)
{
    static const char hex[] PROGMEM = "0123456789ABCDEF";

    for(uint8_t i = 0; i < n; i++)
    {
        uart_tx_char(pgm_read_byte(&hex[b[i] >> 4]));
        uart_tx_char(pgm_read_byte(&hex[b[i] & 0x0F]));
        uart_tx_char(' ');
    }

    uart_tx_string_P(PSTR("\r\n"));
}
#endif

void eeprom_init(void)
{
    uint8_t buf[8];
    uint8_t found = 0;

    eeprom_slave_addr = EEPROM_BASE_ADDR;
    eeprom_present = eeprom_ack(eeprom_slave_addr);
    // Si no responde en la direccion esperada, prueba las 8
    // combinaciones de A2..A0 (0x50-0x57) por si cambia el cableado.
    if(!eeprom_present)
    {
        for(uint8_t a = 0x50; a <= 0x57; a++)
        {
            if(eeprom_ack(a))
            {
                eeprom_slave_addr = a;
                eeprom_present = 1;
                break;
            }
        }
    }

    if(!eeprom_present)
        return;

#if EEPROM_DEBUG
    uart_tx_string_P(PSTR("EEPROM addr=0x"));
    {
        static const char hex[] PROGMEM = "0123456789ABCDEF";
        uart_tx_char(pgm_read_byte(&hex[eeprom_slave_addr >> 4]));
        uart_tx_char(pgm_read_byte(&hex[eeprom_slave_addr & 0x0F]));
    }
    uart_tx_string_P(PSTR("\r\n"));
#endif

    for(uint8_t i = 0; i < EEPROM_HDR_SLOTS; i++)
    {
        uint16_t magic;
        uint32_t count;

        eeprom_read((uint16_t)(i * 8), buf, 8);

        magic = (uint16_t)(buf[0] | (buf[1] << 8));

        if(magic != EEPROM_HDR_MAGIC)
            continue;

        count = (uint32_t)buf[4]
              | ((uint32_t)buf[5] << 8)
              | ((uint32_t)buf[6] << 16)
              | ((uint32_t)buf[7] << 24);

        // Se queda con la copia mas reciente.
        if(!found || (int32_t)(count - rec_count) > 0)
        {
            rec_count = count;
            hdr_slot = i;
            found = 1;
        }
    }

    if(!found)
    {
        uint8_t check[8];

        hdr_slot = 0;
        rec_count = 0;
        eeprom_write_hdr(0, 0);

        // El chip ACKa aunque no acepte la escritura (WP); se
        // comprueba releyendo la cabecera recien grabada.
        eeprom_read(0, check, 8);
        if((uint16_t)(check[0] | (check[1] << 8)) != EEPROM_HDR_MAGIC)
        {
            eeprom_wr_fail = 1;

#if EEPROM_DEBUG
            uart_tx_string_P(PSTR("EEPROM hdr read: "));
            eeprom_tx_hex(check, 8);
#endif
        }
    }
}

void eeprom_log_ext(uint32_t uptime_s, float wind_kmh, float temp_c, float hum_rh, float press_hpa, uint32_t rain_tips)
{
    uint8_t buf[EEPROM_REC_SIZE];
    uint16_t wind_x10;
    int16_t temp_x10;
    uint16_t hum_x10;
    uint32_t press_x10;
    uint16_t slot;
    uint16_t addr;

    if(!eeprom_present)
        return;

    if(wind_kmh < 0) wind_kmh = 0;
    wind_x10 = (uint16_t)(wind_kmh * 10.0f + 0.5f);
    if(temp_c >= 0.0f)
        temp_x10 = (int16_t)(temp_c * 10.0f + 0.5f);
    else
        temp_x10 = (int16_t)(temp_c * 10.0f - 0.5f);
    if(hum_rh < 0) hum_rh = 0;
    if(hum_rh > 100) hum_rh = 100;
    hum_x10 = (uint16_t)(hum_rh * 10.0f + 0.5f);
    if(press_hpa < 800.0f) press_hpa = 800.0f;
    if(press_hpa > 1200.0f) press_hpa = 1200.0f;
    press_x10 = (uint32_t)(press_hpa * 10.0f + 0.5f);

    memset(buf, 0, sizeof(buf));

    buf[0] = (uint8_t)(uptime_s);
    buf[1] = (uint8_t)(uptime_s >> 8);
    buf[2] = (uint8_t)(uptime_s >> 16);
    buf[3] = (uint8_t)(uptime_s >> 24);
    buf[4] = (uint8_t)(wind_x10);
    buf[5] = (uint8_t)(wind_x10 >> 8);
    buf[6] = (uint8_t)(temp_x10);
    buf[7] = (uint8_t)(temp_x10 >> 8);
    buf[8] = (uint8_t)(rain_tips);
    buf[9] = (uint8_t)(rain_tips >> 8);
    buf[10] = (uint8_t)(rain_tips >> 16);
    buf[11] = (uint8_t)(rain_tips >> 24);
    buf[12] = (uint8_t)(hum_x10);
    buf[13] = (uint8_t)(hum_x10 >> 8);
    buf[14] = (uint8_t)(press_x10);
    buf[15] = (uint8_t)(press_x10 >> 8);
    buf[16] = (uint8_t)(press_x10 >> 16);
    buf[17] = (uint8_t)(press_x10 >> 24);

    slot = (uint16_t)(rec_count % EEPROM_REC_MAX);
    addr = (uint16_t)(EEPROM_HDR_PAGE + (uint32_t)slot * EEPROM_REC_SIZE);

    eeprom_write(addr, buf, EEPROM_REC_SIZE);

    if(eeprom_verify(addr, buf, EEPROM_REC_SIZE))
    {
        eeprom_wr_fail = 1;
        return;
    }

    rec_count++;
    hdr_slot = (uint8_t)((hdr_slot + 1) % EEPROM_HDR_SLOTS);
    eeprom_write_hdr(hdr_slot, rec_count);
}

void eeprom_log(uint32_t uptime_s, float wind_kmh, float temp_c, uint32_t rain_tips)
{
    eeprom_log_ext(uptime_s, wind_kmh, temp_c, 0.0f, 1013.25f, rain_tips);
}


void eeprom_dump(void)
{
    uint8_t buf[EEPROM_REC_SIZE];
    uint32_t total;
    uint32_t start;
    uint32_t i;

    if(!eeprom_present)
    {
        uart_tx_string_P(PSTR("EEPROM ABSENT\r\n"));
        return;
    }

    if(eeprom_wr_fail)
    {
        // El chip responde en el bus pero no conserva lo escrito.
        uart_tx_string_P(PSTR("EEPROM NO_ESCRITURA (revisar WP/GND)\r\n"));
    }

    total = rec_count;
    if(total > EEPROM_REC_MAX)
        total = EEPROM_REC_MAX;

    start = (rec_count > EEPROM_REC_MAX) ? (rec_count % EEPROM_REC_MAX) : 0;

    uart_tx_string_P(PSTR("LOG_COUNT="));
    uart_tx_uint32(rec_count);
    uart_tx_string_P(PSTR("\r\n"));

    for(i = 0; i < total; i++)
    {
        uint32_t slot = (start + i) % EEPROM_REC_MAX;
        uint16_t addr = (uint16_t)(EEPROM_HDR_PAGE + slot * EEPROM_REC_SIZE);
        uint32_t uptime;
        uint16_t wind_x10;
        int16_t temp_x10;
        uint32_t rain;
        uint16_t hum_x10 = 0;
        uint32_t press_x10 = 101325;

        eeprom_read(addr, buf, EEPROM_REC_SIZE);

        uptime = (uint32_t)buf[0]
               | ((uint32_t)buf[1] << 8)
               | ((uint32_t)buf[2] << 16)
               | ((uint32_t)buf[3] << 24);

        wind_x10 = (uint16_t)(buf[4] | (buf[5] << 8));
        temp_x10 = (int16_t)(buf[6] | (buf[7] << 8));

        rain = (uint32_t)buf[8]
             | ((uint32_t)buf[9] << 8)
             | ((uint32_t)buf[10] << 16)
             | ((uint32_t)buf[11] << 24);
        if(total > 0) // new format
        {
            hum_x10 = (uint16_t)(buf[12] | (buf[13] << 8));
            press_x10 = (uint32_t)buf[14]
                      | ((uint32_t)buf[15] << 8)
                      | ((uint32_t)buf[16] << 16)
                      | ((uint32_t)buf[17] << 24);
        }

        uart_tx_string_P(PSTR("LOG "));
        uart_tx_time(uptime);
        uart_tx_char(' ');
        uart_tx_decimal((int16_t)wind_x10);
        uart_tx_char(' ');
        uart_tx_decimal(temp_x10);
        uart_tx_char(' ');
        uart_tx_uint32(rain);
        uart_tx_char(' ');
        uart_tx_decimal((int16_t)hum_x10);
        uart_tx_char(' ');
        uart_tx_decimal((int16_t)(press_x10 / 10));
        uart_tx_string_P(PSTR("\r\n"));
    }

    uart_tx_string_P(PSTR("LOG_END\r\n"));
}

uint32_t eeprom_count(void)
{
    return rec_count;
}
