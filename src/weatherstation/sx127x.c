#include <avr/io.h>
#include <util/delay.h>
#include "spi.h"
#include "sx127x.h"

// Registros SX127x
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_OCP                  0x0B
#define REG_LNA                  0x0C
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_MODEM_CONFIG1        0x1D
#define REG_MODEM_CONFIG2        0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MAX_PAYLOAD_LENGTH   0x23
#define REG_MODEM_CONFIG3        0x26
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_INVERT_IQ            0x33
#define REG_SYNC_WORD            0x39
#define REG_DIO_MAPPING1         0x40
#define REG_VERSION              0x42

#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03
#define MODE_RX_CONT             0x05
#define MODE_LORA                0x80

#define IRQ_RX_DONE              0x40
#define IRQ_CRC_ERROR            0x20
#define IRQ_TX_DONE              0x08

#define SX127X_VERSION           0x12

// FRF = freq * 2^19 / 32 MHz
#define SX127X_FRF ((uint32_t)(((uint64_t)SX127X_FREQ_HZ * 524288ULL) / 32000000ULL))

#define CS_LOW()   (SX127X_CS_PORT &= (uint8_t)~(1 << SX127X_CS_BIT))
#define CS_HIGH()  (SX127X_CS_PORT |= (uint8_t)(1 << SX127X_CS_BIT))

static uint8_t sx127x_write_reg(uint8_t reg, uint8_t val)
{
    CS_LOW();
    spi_transfer((uint8_t)(reg | 0x80));
    spi_transfer(val);
    CS_HIGH();
    return val;
}

static uint8_t sx127x_read_reg(uint8_t reg)
{
    uint8_t val;

    CS_LOW();
    spi_transfer((uint8_t)(reg & 0x7F));
    val = spi_transfer(0x00);
    CS_HIGH();

    return val;
}

static void sx127x_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    CS_LOW();
    spi_transfer((uint8_t)(reg | 0x80));

    for(uint8_t i = 0; i < len; i++)
        spi_transfer(buf[i]);

    CS_HIGH();
}

static void sx127x_read_burst(uint8_t reg, uint8_t *buf, uint8_t len)
{
    CS_LOW();
    spi_transfer((uint8_t)(reg & 0x7F));

    for(uint8_t i = 0; i < len; i++)
        buf[i] = spi_transfer(0x00);

    CS_HIGH();
}

static void sx127x_standby(void)
{
    sx127x_write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_STDBY));
}

static uint8_t sx127x_wait_irq(uint8_t mask, uint16_t timeout_ms)
{
    while(timeout_ms--)
    {
        uint8_t flags = sx127x_read_reg(REG_IRQ_FLAGS);

        if(flags & mask)
            return flags;

        _delay_ms(1);
    }

    return 0;
}

uint8_t sx127x_init(void)
{
    uint8_t version;

    // CS en alto antes de tocar el bus (ISP comparte pines).
    SX127X_CS_DDR |= (uint8_t)(1 << SX127X_CS_BIT);
    SX127X_CS_PORT |= (uint8_t)(1 << SX127X_CS_BIT);

    SX127X_RST_DDR |= (uint8_t)(1 << SX127X_RST_BIT);
    SX127X_RST_PORT &= (uint8_t)~(1 << SX127X_RST_BIT);
    _delay_ms(1);
    SX127X_RST_PORT |= (uint8_t)(1 << SX127X_RST_BIT);
    _delay_ms(10);

    spi_init();

    version = sx127x_read_reg(REG_VERSION);
    if(version != SX127X_VERSION)
        return 0;

    sx127x_write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_SLEEP));
    _delay_ms(2);
    sx127x_standby();

    // Frecuencia
    sx127x_write_reg(REG_FRF_MSB, (uint8_t)(SX127X_FRF >> 16));
    sx127x_write_reg(REG_FRF_MID, (uint8_t)(SX127X_FRF >> 8));
    sx127x_write_reg(REG_FRF_LSB, (uint8_t)(SX127X_FRF));

    // FIFO
    sx127x_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    sx127x_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);
    sx127x_write_reg(REG_FIFO_ADDR_PTR, 0x00);

    // Modulacion: BW 125 kHz, CR 4/5, cabecera explicita, SF9, CRC on
    sx127x_write_reg(REG_MODEM_CONFIG1, 0x72);
    sx127x_write_reg(REG_MODEM_CONFIG2, 0x94);
    sx127x_write_reg(REG_MODEM_CONFIG3, 0x04);
    sx127x_write_reg(REG_PREAMBLE_MSB, 0x00);
    sx127x_write_reg(REG_PREAMBLE_LSB, 0x08);
    sx127x_write_reg(REG_SYNC_WORD, 0x12);
    sx127x_write_reg(REG_MAX_PAYLOAD_LENGTH, SX127X_MAX_PAYLOAD);
    sx127x_write_reg(REG_DETECTION_OPTIMIZE, 0xC3);
    sx127x_write_reg(REG_INVERT_IQ, 0x27);

    // Ganancia LNA maxima y PA_BOOST ~14 dBm
    sx127x_write_reg(REG_LNA, 0x23);
    sx127x_write_reg(REG_PA_CONFIG, 0xFC);
    sx127x_write_reg(REG_OCP, 0x2B);

    sx127x_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx127x_set_rx();

    return 1;
}

void sx127x_set_rx(void)
{
    sx127x_standby();
    sx127x_write_reg(REG_DIO_MAPPING1, 0x00);   // DIO0 = RxDone
    sx127x_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx127x_write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_RX_CONT));
}

void sx127x_sleep(void)
{
    sx127x_write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_SLEEP));
}

uint8_t sx127x_send(const uint8_t *data, uint8_t len)
{
    uint8_t ok;

    if(len > SX127X_MAX_PAYLOAD)
        return 0;

    sx127x_standby();
    sx127x_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    sx127x_write_reg(REG_PAYLOAD_LENGTH, len);
    sx127x_write_burst(REG_FIFO, data, len);

    sx127x_write_reg(REG_DIO_MAPPING1, 0x40);   // DIO0 = TxDone
    sx127x_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx127x_write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_TX));

    ok = (uint8_t)(sx127x_wait_irq(IRQ_TX_DONE, 3000) != 0);

    sx127x_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx127x_set_rx();

    return ok;
}

uint8_t sx127x_poll_rx(uint8_t *buf, uint8_t *len)
{
    uint8_t flags = sx127x_read_reg(REG_IRQ_FLAGS);
    uint8_t n;
    uint8_t addr;

    if(!(flags & IRQ_RX_DONE))
        return 0;

    if(flags & IRQ_CRC_ERROR)
    {
        sx127x_set_rx();
        return 0;
    }

    n = sx127x_read_reg(REG_RX_NB_BYTES);

    if(n > *len)
        n = *len;

    addr = sx127x_read_reg(REG_FIFO_RX_CURRENT_ADDR);
    sx127x_write_reg(REG_FIFO_ADDR_PTR, addr);
    sx127x_read_burst(REG_FIFO, buf, n);

    *len = n;

    sx127x_set_rx();
    return 1;
}
