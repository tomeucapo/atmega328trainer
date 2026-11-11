#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <stdint.h>

typedef struct {
    volatile uint32_t *rain_count;
    float *wind_speed;
    float *current_temp;
    float *bme_temp;
    float *bme_hum;
    float *bme_press;
    uint8_t *bme_present;
    uint8_t *view_mode;
    uint8_t mcp_reserved_pin;
    void (*print_ext_status)(uint8_t index, uint8_t numbered);
} serial_command_context_t;

void serial_commands_init(const serial_command_context_t *context);
void task_serial(void);

#endif
