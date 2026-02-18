project(avr_project C ASM)

cmake_minimum_required(VERSION 3.22)

# MCU y frecuencia
set(MCU atmega328p)
set(F_CPU 8000000UL)

# Nombre del ejecutable
set(TARGET firmware)

# ==============================
# Toolchain AVR
# ==============================

# Ejecutables AVR
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_ASM_COMPILER avr-gcc)
set(CMAKE_OBJCOPY avr-objcopy)
set(CMAKE_SIZE avr-size)

# ==============================
# Flags de compilación
# ==============================

add_compile_options(
    -mmcu=${MCU}
    -DF_CPU=${F_CPU}
    -Os
    -Wall
    -std=gnu11
)

add_link_options(
    -mmcu=${MCU}
)

# ==============================
# Archivos fuente
# ==============================

file(GLOB SRC_FILES
    anemometre/*.c
)

add_executable(${TARGET}.elf ${SRC_FILES})

# ==============================
# Generar archivo HEX
# ==============================

add_custom_command(
    TARGET ${TARGET}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O ihex
        -R .eeprom
        ${TARGET}.elf
        ${TARGET}.hex
)

# Mostrar tamaño
add_custom_command(
    TARGET ${TARGET}.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=avr --mcu=${MCU} ${TARGET}.elf
)

# ==============================
# Target para flashear (avrdude)
# ==============================

add_custom_target(flash
    COMMAND avrdude
        -p ${MCU}
        -c dragon_isp
        -P usb
        -U flash:w:${TARGET}.hex:i
    DEPENDS ${TARGET}.elf
)
