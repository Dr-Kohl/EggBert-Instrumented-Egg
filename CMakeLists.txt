cmake_minimum_required(VERSION 3.13)

# RP2354A uses the RP2350 family. Override PICO_SDK_PATH if your SDK lives elsewhere.
set(PICO_PLATFORM rp2350 CACHE STRING "Pico SDK platform")

if (NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "C:/Users/Kohlc/AppData/Local/Arduino15/packages/rp2040/hardware/rp2040/5.6.0/pico-sdk"
        CACHE PATH "Path to the Pico SDK")
else()
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE PATH "Path to the Pico SDK")
endif()

# Arduino-Pico already provides a suitable ARM GNU toolchain on this machine.
# An explicit -DPICO_TOOLCHAIN_PATH remains supported for other installations.
if (NOT DEFINED PICO_TOOLCHAIN_PATH)
    set(PICO_TOOLCHAIN_PATH "C:/Users/Kohlc/AppData/Local/Arduino15/packages/rp2040/tools/pqt-gcc/4.1.0-1aec55e/bin"
        CACHE PATH "Directory containing arm-none-eabi-gcc")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(eggbert_bringup C CXX ASM)
pico_sdk_init()

add_executable(eggbert_bringup
    src/main.c
    src/ssd1306.c
)

target_include_directories(eggbert_bringup PRIVATE include)
target_link_libraries(eggbert_bringup PRIVATE
    pico_stdlib
    hardware_gpio
    hardware_i2c
)

# USB CDC gives a serial log; no physical UART wiring is assumed.
pico_enable_stdio_usb(eggbert_bringup 1)
pico_enable_stdio_uart(eggbert_bringup 0)
pico_add_extra_outputs(eggbert_bringup)
