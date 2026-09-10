# ATMega328P Trainer

Basic board for practicing with the ATMega328P microcontroller. It includes an optocoupled input designed to connect pulse sensors at different voltages, such as an anemometer that operates at 24VDC. It also includes a USB-to-serial adapter that allows you to work with the ATMega's UART.

https://www.pcbway.com/project/shareproject/Anemometer_with_ATMega128P_fbefd825.html

## Weather station base

This first version supports wind, rain and temperature sensors can read from USB port with simple commands:

```
@STAT_TEMP
@STAT_RAIN
@STAT_ANEMO
```

Also can reset RAIN counter with @RESET_RAIN.

Temperature sensor is digital serial sensor DS18B20, one bit data serial contected to PB2 port.
