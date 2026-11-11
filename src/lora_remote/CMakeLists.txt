cmake_minimum_required(VERSION 3.16)

# Nodo remoto LoRa: ATmega328P a 3.3 V / 8 MHz.
set(MCU atmega328p)
set(F_CPU 8000000UL)
set(LORA_NODE_ID 1 CACHE STRING "ID de este nodo LoRa (1 o 2)")
option(LORA_DEBUG_UART "Enviar diagnostico por UART en PD1/TXD" OFF)

if(NOT LORA_NODE_ID STREQUAL "1" AND NOT LORA_NODE_ID STREQUAL "2")
    message(FATAL_ERROR "LORA_NODE_ID debe ser 1 o 2")
endif()

# ==============================
# Toolchain AVR (antes de project())
# ==============================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)

set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_ASM_COMPILER avr-gcc)
set(CMAKE_OBJCOPY avr-objcopy)
set(CMAKE_SIZE avr-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(lora_remote C ASM)

set(TARGET firmware_remote)
set(WS ../weatherstation)

add_compile_options(
    -mmcu=${MCU}
    -DF_CPU=${F_CPU}
    -DLORA_NODE_ID=${LORA_NODE_ID}
    -Os
    -Wall
    -std=gnu11
)

if(LORA_DEBUG_UART)
    add_compile_options(-DLORA_DEBUG_UART=1)
else()
    add_compile_options(-DLORA_DEBUG_UART=0)
endif()

add_link_options(
    -mmcu=${MCU}
)

add_executable(${TARGET}.elf
    main.c
    ${WS}/bme280.c
    ${WS}/twi.c
    ${WS}/spi.c
    ${WS}/sx127x.c
    ${WS}/lora_proto.c
)

target_include_directories(${TARGET}.elf PRIVATE ${WS})

add_custom_command(
    TARGET ${TARGET}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O ihex
        -R .eeprom
        ${TARGET}.elf
        ${TARGET}.hex
)

add_custom_command(
    TARGET ${TARGET}.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=avr --mcu=${MCU} ${TARGET}.elf
)

add_custom_target(flash
    COMMAND avrdude
        -p ${MCU}
        -c dragon_isp
        -P usb
        -U flash:w:${TARGET}.hex:i
    DEPENDS ${TARGET}.elf
)
