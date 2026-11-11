#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ds18b20.h"
#include "eeprom.h"
#include "mcp23017.h"
#include "prediction.h"
#include "serial_commands.h"
#include "uart.h"

#define RAIN_MM_PER_TIP 0.3f

static const serial_command_context_t *command_context;

void serial_commands_init(const serial_command_context_t *context)
{
    command_context = context;
}

static uint8_t parse_mcp_pin(const char *s, uint8_t *pin)
{
    uint8_t value = 0;

    while(*s == ' ')
        s++;

    if(*s < '0' || *s > '9')
        return 0;

    while(*s >= '0' && *s <= '9')
    {
        value = value * 10 + (*s - '0');

        if(value > 7)
            return 0;

        s++;
    }

    if(*s != '\0')
        return 0;

    *pin = value;
    return 1;
}

// @MCP_ON / @MCP_OFF <pin> | A<pin> | B<pin>
// Puerto por defecto A; el pin del pulsador esta reservado.
static void handle_mcp_cmd(const char *arg, uint8_t value)
{
    char buffer[8];
    char port;
    uint8_t pin;

    while(*arg == ' ')
        arg++;

    if(*arg == 'B' || *arg == 'b')
    {
        port = 'B';
        arg++;
    }
    else
    {
        port = 'A';

        if(*arg == 'A' || *arg == 'a')
            arg++;
    }

    if(!parse_mcp_pin(arg, &pin) ||
       (port == 'A' && pin == command_context->mcp_reserved_pin))
    {
        uart_tx_string_P(PSTR("ERR\r\n"));
        return;
    }

    mcp23017_write_pin(port == 'B' ? MCP23017_PORTB : MCP23017_PORTA,
                       pin, value);

    if(value)
        uart_tx_string_P(PSTR("MCP_ON="));
    else
        uart_tx_string_P(PSTR("MCP_OFF="));

    buffer[0] = port;
    buffer[1] = 0;
    uart_tx_string(buffer);

    utoa(pin, buffer, 10);
    uart_tx_string(buffer);
    uart_tx_string_P(PSTR("\r\n"));
}

static void handle_command(const char *command)
{
    // Mantenerlo estatico evita sumar 32 B al pico de uso de pila en AVR.
    static char buffer[32];

    if(strcmp(command, "@GET_ANEMO") == 0)
    {
        dtostrf(*command_context->wind_speed, 6, 3, buffer);
        uart_tx_string_P(PSTR("ANEMO="));
        uart_tx_string(buffer);
        uart_tx_string_P(PSTR(" km/h\r\n"));
    }
    else if(strcmp(command, "@GET_RAIN") == 0)
    {
        uint32_t count;
        float mm;

        cli();
        count = *command_context->rain_count;
        sei();
        mm = count * RAIN_MM_PER_TIP;

        uart_tx_string_P(PSTR("RAIN_COUNT="));
        ultoa(count, buffer, 10);
        uart_tx_string(buffer);
        uart_tx_string_P(PSTR("\r\nRAIN_MM="));
        dtostrf(mm, 6, 2, buffer);
        uart_tx_string(buffer);
        uart_tx_string_P(PSTR("\r\n"));
    }
    else if(strcmp(command, "@GET_TEMP") == 0)
    {
        float temp = ds18b20_get_temp();

        if(temp == DS18B20_ERROR_TEMP)
            uart_tx_string_P(PSTR("TEMP_ERR\r\n"));
        else
        {
            uart_tx_string_P(PSTR("TEMP="));
            dtostrf(temp, 6, 3, buffer);
            uart_tx_string(buffer);
            uart_tx_string_P(PSTR(" C\r\n"));
        }
    }
    else if(strcmp(command, "@RESET_RAIN") == 0)
    {
        cli();
        *command_context->rain_count = 0;
        sei();
        uart_tx_string_P(PSTR("RAIN RESET OK\r\n"));
    }
    else if(strcmp(command, "@DUMP") == 0)
    {
        eeprom_dump();
    }
    else if(strcmp(command, "@VIEW") == 0)
    {
        *command_context->view_mode ^= 1;
        if(*command_context->view_mode)
            uart_tx_string_P(PSTR("VIEW=GRAPH\r\n"));
        else
            uart_tx_string_P(PSTR("VIEW=TEXT\r\n"));
    }
    else if(strcmp(command, "@GET_BME") == 0)
    {
        if(!*command_context->bme_present)
            uart_tx_string_P(PSTR("BME280_NOT_FOUND\r\n"));
        else
        {
            char pb[16];
            float dp;

            uart_tx_string_P(PSTR("BME_TEMP="));
            dtostrf(*command_context->bme_temp, 6, 2, pb);
            uart_tx_string(pb);
            uart_tx_string_P(PSTR(" C\r\nBME_HUM="));
            dtostrf(*command_context->bme_hum, 5, 1, pb);
            uart_tx_string(pb);
            uart_tx_string_P(PSTR(" %RH\r\nBME_PRESS="));
            dtostrf(*command_context->bme_press, 7, 2, pb);
            uart_tx_string(pb);
            uart_tx_string_P(PSTR(" hPa\r\n"));
            dp = pred_dewpoint(*command_context->bme_temp > -50 ?
                               *command_context->bme_temp :
                               *command_context->current_temp,
                               *command_context->bme_hum);
            uart_tx_string_P(PSTR("DEWPOINT="));
            dtostrf(dp, 6, 2, pb);
            uart_tx_string(pb);
            uart_tx_string_P(PSTR(" C\r\n"));
        }
    }
    else if(strcmp(command, "@GET_EXT") == 0)
    {
        command_context->print_ext_status(0, 0);
    }
    else if(strcmp(command, "@GET_EXT1") == 0)
    {
        command_context->print_ext_status(0, 1);
    }
    else if(strcmp(command, "@GET_EXT2") == 0)
    {
        command_context->print_ext_status(1, 1);
    }
    else if(strcmp(command, "@FORECAST") == 0)
    {
        char fbuf[48];
        pred_forecast(fbuf, sizeof(fbuf));
        uart_tx_string(fbuf);
        uart_tx_string_P(PSTR("\r\n"));
    }
    else if(strncmp(command, "@MCP_ON", 7) == 0)
    {
        handle_mcp_cmd(command + 7, 1);
    }
    else if(strncmp(command, "@MCP_OFF", 8) == 0)
    {
        handle_mcp_cmd(command + 8, 0);
    }
    else
    {
        uart_tx_string_P(PSTR("ERR\r\n"));
    }
}

// Lee el UART y procesa los comandos terminados en CR/LF.
void task_serial(void)
{
    static char rx_buffer[32];
    static uint8_t rx_index = 0;

    while(uart_rx_available())
    {
        char c = uart_rx_read();
        uart_tx_char(c);

        if(c == '\r' || c == '\n')
        {
            rx_buffer[rx_index] = 0;
            rx_index = 0;
            handle_command(rx_buffer);
        }
        else if(rx_index < sizeof(rx_buffer) - 1)
        {
            rx_buffer[rx_index++] = c;
        }
    }
}
