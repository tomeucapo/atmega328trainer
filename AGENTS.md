# AGENTS.md

AVR firmware (ATmega328P, 8 MHz) for a weather station (anemometer + rain gauge + DS18B20 temperature) that talks UART serial. Silver lining: this is a small single-firmware repo, so most context below is build/hardware specifics an agent would otherwise guess wrong.

## Build (firmware)

CMake cross-toolchain for AVR lives in `src/CMakeLists.txt`. Build from `src/`:

```sh
cmake -S . -B build && cmake --build build
```

Outputs `build/firmware.elf` and `build/firmware.hex` (`src/build/` is gitignored; flash target runs `avrdude -c dragon_isp` but `avrdude` is not installed on this machine).

- Requires `gcc-avr`, `avr-libc`, and CMake. On this dev box `avr-libc` is **not** installed system-wide (only `gcc-avr`/`binutils-avr`); the build only links if the libc is reachable. Either `sudo apt install gcc-avr avr-libc`, or point at a local extraction:
  ```sh
  AVR=/path/to/extracted/usr/lib/avr
  cmake -S . -B build \
    -DCMAKE_C_FLAGS="-B $AVR/lib -I $AVR/include" \
    -DCMAKE_EXE_LINKER_FLAGS="-B $AVR/lib -L $AVR/lib/avr5"
  ```
  `avr5` is the multilib subdir for `atmega328p` (needed for `crtatmega328p.o`, `-lc`, `-lm`). These flags are configure-time only and are NOT in `CMakeLists.txt`.
- CMake's toolchain settings were moved **before** `project()` in `CMakeLists.txt`; without that, CMake detects the host `cc` instead of `avr-gcc`. A stale `build/` cache pins a previously detected compiler: if the log shows `/usr/bin/cc`, delete `build/` and reconfigure.
- Toolchain: `-mmcu=atmega328p -DF_CPU=8000000UL -Os -Wall -std=gnu11`. Keep `F_CPU` correct: `_delay_*` and UBRR maths depend on it. `main.c` guards its local `F_CPU` with `#ifndef` because CMake also passes it via `-D`.
- No lint/test targets for the firmware. Verification = compile cleanly.

## Serial protocol / host test

- UART: 9600 8N1. Commands (CR/LF terminated): `@STAT_ANEMO`, `@STAT_RAIN`, `@STAT_TEMP`, `@RESET_RAIN`. Replies are plain text lines: `ANEMO=<v> km/h`, `RAIN_COUNT=...`, `RAIN_MM=...`, `RAIN_LM2=...`, `TEMP=<v> C`, `TEMP_ERR`, `ERR`.
- `src/test.py` is the only test harness: host-side reader against a real board on `/dev/ttyUSB0` (needs `python3-serial`). It polls `@STAT_ANEMO`/`@STAT_RAIN` in a loop.

## Firmware structure and hardware wiring

- `src/anemometre/main.c` — UART, timers, rain ISR, serial command loop. TIMER1 input capture (ICP1) measures anemometer period; TIMER0 1 ms gives `millis_counter` and the LED blink (PD6).
- `src/anemometre/onewire.c` + `.h` — bit-banged 1-Wire. Timing tuned for F_CPU 8 MHz; interrupts are disabled (`cli()`/`sei()`) around each bit slot, so keep timings unchanged if touching. The DS18B20 pin defines (`DS18B20_DDR/PORT/PIN/BIT`) live in `onewire.h`.
- `src/anemometre/ds18b20.c` + `.h` — sets 9-bit resolution (~94 ms conversion) at init; `ds18b20_get_temp()` blocks ~100 ms and returns `DS18B20_ERROR_TEMP` (`-127.0f`) on a missing/failed sensor.
- Pin map: PB0 = anemometer (ICP1), PB1 = rain gauge reed switch (PCINT1, NC contact, 100 ms debounce), PB2 = DS18B20 1-Wire (needs external 4.7 kΩ pull-up to VCC).

## Conventions

- Code comments are in Spanish; keep that style.
- `anemometre/main.c.old` / Microchip Studio remnants were deleted; don't reintroduce the single-file `main.c` that bundled the OneWire code — keep the split `onewire.c`/`ds18b20.c`.
- Schematic (KiCad) and gerbers live in `schematic/`; the board has USB-to-serial and an optocoupled 24 V-capable input.