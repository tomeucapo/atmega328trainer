#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "twi.h"
#include "bme280.h"
#include "sx127x.h"
#include "lora_proto.h"

// ======================================================
// NODO REMOTO LORA: BME280 (temperatura/humedad/presion)
// ATmega328P a 3.3 V / 8 MHz (RC interno o cristal de 8 MHz).
// Duerme entre transmisiones para ahorrar bateria.
// ======================================================

#define TX_PERIOD_S 60
#define WDT_PERIOD_S 8

#define LED_DDR  DDRB
#define LED_PORT PORTB
#define LED_BIT  PB0

ISR(WDT_vect)
{
    // Solo despierta del power-down.
}

static void led_blink(uint8_t n)
{
    LED_DDR |= (1 << LED_BIT);

    while(n--)
    {
        LED_PORT |= (1 << LED_BIT);
        _delay_ms(40);
        LED_PORT &= (uint8_t)~(1 << LED_BIT);
        _delay_ms(160);
    }
}

static void sleep_8s(void)
{
    cli();
    wdt_reset();

    // Secuencia temporizada: WDIE solo (interrupcion, sin reset).
    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDIE) | (1 << WDP3) | (1 << WDP0);

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
    wdt_disable();
}

static void sleep_period(void)
{
    for(uint8_t i = 0; i < (TX_PERIOD_S + WDT_PERIOD_S - 1) / WDT_PERIOD_S; i++)
        sleep_8s();
}

// Mide Vcc contra la referencia interna de 1.1 V (sin divisor).
static uint16_t read_vcc_mv(void)
{
    uint16_t adc;

    power_adc_enable();

    ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    _delay_ms(2);

    ADCSRA |= (1 << ADSC);
    while(ADCSRA & (1 << ADSC))
        ;

    adc = ADC;
    ADCSRA = 0;

    power_adc_disable();

    if(adc == 0)
        return 0;

    return (uint16_t)(1125300UL / adc);
}

int main(void)
{
    uint16_t seq = 0;

    // El nodo no usa UART ni timers.
    power_usart0_disable();
    power_timer0_disable();
    power_timer1_disable();
    power_timer2_disable();

    LED_DDR |= (1 << LED_BIT);

    twi_bus_recover();
    _delay_ms(50);
    twi_init();

    // BME280: si no responde, reintenta (el sensor puede arrancar
    // despues que el MCU).
    while(!bme280_init())
    {
        led_blink(2);
        _delay_ms(500);
    }
    bme280_sleep();

    // SX127x: idem.
    while(!sx127x_init())
    {
        led_blink(3);
        _delay_ms(500);
    }

    sei();

    while(1)
    {
        bme280_data_t d;
        lora_weather_t frame;

        if(bme280_forced_read(&d) == 0)
        {
            int16_t temp_x10;
            uint16_t hum_x10;
            uint16_t press_hpa;

            if(d.temperature >= 0.0f)
                temp_x10 = (int16_t)(d.temperature * 10.0f + 0.5f);
            else
                temp_x10 = (int16_t)(d.temperature * 10.0f - 0.5f);

            hum_x10 = (uint16_t)(d.humidity * 10.0f + 0.5f);
            press_hpa = (uint16_t)(d.pressure + 0.5f);

            lora_build_weather(&frame, seq, temp_x10, hum_x10,
                               press_hpa, read_vcc_mv());

            LED_PORT |= (1 << LED_BIT);
            sx127x_send((const uint8_t *)&frame, sizeof(frame));
            LED_PORT &= (uint8_t)~(1 << LED_BIT);

            seq++;
        }
        else
        {
            led_blink(1);
        }

        sx127x_sleep();
        sleep_period();
    }
}
