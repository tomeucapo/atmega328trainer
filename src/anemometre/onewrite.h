#ifndef ONEWIRE_H
#define ONEWIRE_H

static void onewire_low(void);
static void onewire_release(void);
static uint8_t onewire_read_pin(void);
static uint8_t onewire_reset(void);
static void onewire_write_bit(uint8_t bit);
static uint8_t onewire_read_bit(void);
static void onewire_write_byte(uint8_t byte);
static uint8_t onewire_read_byte(void);

#endif