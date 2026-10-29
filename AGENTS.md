# AGENTS.md

AVR firmware (ATmega328P, 8 MHz) for a weather station (anemometer + rain gauge + DS18B20 + BME280) that talks UART serial, plus a battery-powered LoRa remote node (BME280) that feeds exterior temperature/humidity/pressure to the master.

## Build (firmware)

There are two independent CMake projects: the master and the LoRa remote.

Master (`src/CMakeLists.txt`, sources in `src/weatherstation/*.c`):

```sh
cd src && cmake -S . -B build && cmake --build build
```

Remote (`src/lora_remote/CMakeLists.txt`, reuses the shared drivers):

```sh
cd src/lora_remote && cmake -S . -B build && cmake --build build
```

Outputs `firmware.elf` / `firmware.hex` and `firmware_remote.elf` / `firmware_remote.hex` under each `build/` (all `build/` dirs are gitignored; flash targets run `avrdude -c dragon_isp` but `avrdude` is not installed on this machine).

- Requires `gcc-avr`, `avr-libc`, and CMake. On this dev box `avr-libc` is **not** installed system-wide (only `gcc-avr`/`binutils-avr`); the build only links if the libc is reachable. Either `sudo apt install gcc-avr avr-libc`, or point at a local extraction:
  ```sh
  AVR=/path/to/extracted/usr/lib/avr
  cmake -S . -B build \
    -DCMAKE_C_FLAGS="-B $AVR/lib -I $AVR/include" \
    -DCMAKE_EXE_LINKER_FLAGS="-B $AVR/lib -L $AVR/lib/avr5"
  ```
  `avr5` is the multilib subdir for `atmega328p` (needed for `crtatmega328p.o`, `-lc`, `-lm`). These flags are configure-time only and are NOT in the CMakeLists.
- CMake's toolchain settings are **before** `project()`; without that, CMake detects the host `cc` instead of `avr-gcc`. A stale `build/` cache pins a previously detected compiler: if the log shows `/usr/bin/cc`, delete `build/` and reconfigure.
- Toolchain: `-mmcu=atmega328p -DF_CPU=8000000UL -Os -Wall -std=gnu11`. Keep `F_CPU` correct: `_delay_*`, UBRR maths and 1-Wire timing depend on it. `main.c` guards its local `F_CPU` with `#ifndef` because CMake also passes it via `-D`.
- **RAM is tight**: the master uses ~1692 B of 2048 B (82.6%), including the 1024 B SSD1306 framebuffer. Keep strings in flash with `PSTR()` + the `_P` helpers (`uart_tx_string_P`, `ssd1306_draw_string_P`) and keep prediction samples compact (`pred_sample_t` is integer-scaled). The linker does not enforce the SRAM limit, so watch the `avr-size` output.
- No lint/test targets. Verification = compile cleanly (no warnings).

## Serial protocol / host test

- UART: 9600 8N1 on PD0/PD1 (occupied by the FT232 USB-serial; USB also powers the board, so the hardware UART is not available for RS485/LoRa-UART modules).
- Commands (CR/LF terminated): `@GET_ANEMO`, `@GET_RAIN`, `@GET_TEMP`, `@RESET_RAIN`, `@DUMP`, `@VIEW`, `@GET_BME`, `@GET_EXT`, `@FORECAST`, `@MCP_ON <pin>`, `@MCP_OFF <pin>`.
- Replies are plain text lines: `ANEMO=<v> km/h`, `RAIN_COUNT=...`, `RAIN_MM=...`, `TEMP=<v> C`, `TEMP_ERR`, `BME_TEMP=...`, `EXT_LINK=OK/NO`, `EXT_TEMP=...`, `PRED: ...`, `ERR`.
- `src/test.py` is a host-side reader against a real board on `/dev/ttyUSB0` (needs `python3-serial`); it polls `@STAT_ANEMO`/`@STAT_RAIN` (legacy names, the firmware answers `@GET_*`).
- LoRa point-to-point frame (16 B, packed, little-endian): magic `WX`, type 1, version 1, `seq`, `temp_x10`, `hum_x10`, `press_hpa`, `battery_mv`, CRC-16/CCITT. Defined in `lora_proto.h`; the master marks the link down after `LORA_TIMEOUT_MS` (180 s).

## Firmware structure and hardware wiring

- `src/weatherstation/main.c` — UART, timers, rain ISR, scheduler tasks (`task_lora`, `task_serial`, `task_sensors`, `task_log`, OLED) and serial command loop.
- `src/weatherstation/ds18b20.c` / `onewire.c` — 9-bit DS18B20 (~94 ms blocking read); `onewire.c` timing is tuned for 8 MHz and disables interrupts around bit slots. Pin defines in `onewire.h`.
- `src/weatherstation/bme280.c` / `.h` — BME280 at I2C `0x76`; init reads calibration and sets normal mode. `bme280_sleep()` / `bme280_forced_read()` are used by the remote for low power.
- `src/weatherstation/prediction.c` / `.h` — 12-sample ring buffer (compact integers), dew point, heat index, forecast text (strings in flash).
- `src/weatherstation/eeprom.c` / `.h` — 24LC64 at `0x54`; 24 B record v2: uptime, wind×10, temp×10, rain tips, hum×10, press×10, reserved; magic `0x5A16`.
- `src/weatherstation/spi.c`, `sx127x.c`, `lora_proto.c` — hardware SPI and minimal SX1276/78 (RFM95W) LoRa driver + CRC protocol.
- `src/lora_remote/main.c` — remote node: BME280 + SX127x, transmits every ~64 s (watchdog power-down sleep between frames).
- I2C bus (PC4/PC5, 100 kHz): MCP23017 `0x20`, SSD1306 `0x3C`, EEPROM `0x54`, BME280 `0x76`.
- Master pins: PB0 anemometer (ICP1), PB1 rain (PCINT1, NC, 100 ms debounce), PB2 DS18B20 (4.7 kΩ pull-up), PB3/PB4/PB5 SPI (MOSI/MISO/SCK), PC4/PC5 I2C, PD0/PD1 UART, PD2 SX127x CS, PD3 SX127x RESET, PD6 LED.
- LoRa: RFM95W 868 MHz (UE) or Ra-02 433 MHz (`SX127X_FREQ_HZ`), SF9/BW125/CR4-5/CRC, sync word `0x12`, ~14 dBm. Module is **3.3 V**: the 5 V master needs level shifting and a 3.3 V regulator; keep CS pulled up so ISP programming does not disturb the bus. Remote runs at 3.3 V / 8 MHz (set fuses accordingly).
- `schematic/` holds KiCad sources: `anemometre.kicad_sch` (master), `rs485_bme280_remote/` (older wired-node alternative) and `lora_bme280_remote/` (LoRa remote node: ATmega328P + BME280 + RFM95W-868S2 + LDO 3.3 V). The RFM95W is mounted on two 1x8 headers (`J6`=module pins 1-8, `J7`=pins 9-16, 2.0 mm pitch) with the V2.0 pinout.

## Conventions

- Code comments are in Spanish; keep that style.
- Don't reintroduce the old single-file `main.c` that bundled the OneWire code — keep the split `onewire.c`/`ds18b20.c`.
- The board has USB-to-serial and an optocoupled 24 V-capable input.
