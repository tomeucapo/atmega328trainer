#include <stdlib.h>
#include <string.h>
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
//   offset 64..      registros de 16 B en buffer circular
//
// Registro (16 B, nunca cruza pagina de 32 B):
//   [0..3]  uptime en segundos        (uint32 LE)
//   [4..5]  velocidad * 10 (km/h)     (uint16 LE)
//   [6..7]  temperatura * 10 (C)      (int16  LE)
//   [8..11] contador de lluvia        (uint32 LE)
//   [12..15] reservado
// ======================================================

#define EEPROM_BASE_ADDR 0x54  // A2=1, A1=0, A0=0 (1010 A2 A1 A0)
#define EEPROM_SIZE      8192UL

#define EEPROM_HDR_PAGE  64
#define EEPROM_HDR_SLOTS 8
#define EEPROM_HDR_MAGIC 0x5A15

#define EEPROM_REC_SIZE  16
#define EEPROM_REC_MAX   ((EEPROM_SIZE - EEPROM_HDR_PAGE) / EEPROM_REC_SIZE)

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
            eeprom_wr_fail = 1;
    }
}

void eeprom_log(uint32_t uptime_s, float wind_kmh, float temp_c, uint32_t rain_tips)
{
    uint8_t buf[EEPROM_REC_SIZE];
    uint16_t wind_x10;
    uint16_t temp_x10;
    uint16_t slot;
    uint16_t addr;

    if(!eeprom_present)
        return;

    wind_x10 = (uint16_t)(wind_kmh * 10.0f + 0.5f);
    temp_x10 = (uint16_t)(temp_c * 10.0f + (temp_c >= 0.0f ? 0.5f : -0.5f));

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

    slot = (uint16_t)(rec_count % EEPROM_REC_MAX);
    addr = (uint16_t)(EEPROM_HDR_PAGE + (uint32_t)slot * EEPROM_REC_SIZE);

    eeprom_write(addr, buf, EEPROM_REC_SIZE);

    // Si la relectura no coincide, el registro no se ha guardado
    // (WP activo o fallo de bus): no se cuenta ni avanza la cabecera.
    if(eeprom_verify(addr, buf, EEPROM_REC_SIZE))
    {
        eeprom_wr_fail = 1;
        return;
    }

    rec_count++;
    hdr_slot = (uint8_t)((hdr_slot + 1) % EEPROM_HDR_SLOTS);
    eeprom_write_hdr(hdr_slot, rec_count);
}

static void eeprom_tx_u32(uint32_t v)
{
    char tmp[11];

    ultoa(v, tmp, 10);
    uart_tx_string(tmp);
}

// Imprime un valor con un decimal (v = valor * 10).
static void eeprom_tx_x10(int16_t v)
{
    char tmp[8];
    uint16_t u;

    if(v < 0)
    {
        uart_tx_char('-');
        u = (uint16_t)(-v);
    }
    else
        u = (uint16_t)v;

    itoa(u / 10, tmp, 10);
    uart_tx_string(tmp);
    uart_tx_char('.');
    itoa(u % 10, tmp, 10);
    uart_tx_string(tmp);
}

void eeprom_dump(void)
{
    uint8_t buf[EEPROM_REC_SIZE];
    uint32_t total;
    uint32_t start;
    uint32_t i;

    if(!eeprom_present)
    {
        uart_tx_string("EEPROM ABSENT\r\n");
        return;
    }

    if(eeprom_wr_fail)
    {
        // El chip responde en el bus pero no conserva lo escrito.
        uart_tx_string("EEPROM NO_ESCRITURA (revisar WP/GND)\r\n");
    }

    total = rec_count;
    if(total > EEPROM_REC_MAX)
        total = EEPROM_REC_MAX;

    start = (rec_count > EEPROM_REC_MAX) ? (rec_count % EEPROM_REC_MAX) : 0;

    uart_tx_string("LOG_COUNT=");
    eeprom_tx_u32(rec_count);
    uart_tx_string("\r\n");

    for(i = 0; i < total; i++)
    {
        uint32_t slot = (start + i) % EEPROM_REC_MAX;
        uint16_t addr = (uint16_t)(EEPROM_HDR_PAGE + slot * EEPROM_REC_SIZE);
        uint32_t uptime;
        uint16_t wind_x10;
        int16_t temp_x10;
        uint32_t rain;

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

        uart_tx_string("LOG ");
        eeprom_tx_u32(uptime);
        uart_tx_char(' ');
        eeprom_tx_x10((int16_t)wind_x10);
        uart_tx_char(' ');
        eeprom_tx_x10(temp_x10);
        uart_tx_char(' ');
        eeprom_tx_u32(rain);
        uart_tx_string("\r\n");
    }

    uart_tx_string("LOG_END\r\n");
}

uint32_t eeprom_count(void)
{
    return rec_count;
}
