cmake_minimum_required(VERSION 3.13)

# Preserve command-line/cache/environment choices across the optional extension
# helper, which otherwise sets normal variables over the CMake cache.
set(_eggbert_requested_sdk "${PICO_SDK_PATH}")
set(_eggbert_requested_toolchain "${PICO_TOOLCHAIN_PATH}")
if (NOT _eggbert_requested_sdk AND DEFINED ENV{PICO_SDK_PATH})
    set(_eggbert_requested_sdk "$ENV{PICO_SDK_PATH}")
endif()
if (NOT _eggbert_requested_toolchain AND DEFINED ENV{PICO_TOOLCHAIN_PATH})
    set(_eggbert_requested_toolchain "$ENV{PICO_TOOLCHAIN_PATH}")
endif()

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
if (_eggbert_requested_sdk)
    set(PICO_SDK_PATH "${_eggbert_requested_sdk}")
endif()
if (_eggbert_requested_toolchain)
    set(PICO_TOOLCHAIN_PATH "${_eggbert_requested_toolchain}")
endif()
# Pico 2 supplies the RP2350 defaults; EggBert's pins and 2 MB flash layout are
# specified below and in board.h. The original Pico defaults target RP2040.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# RP2354A uses the RP2350 family. Override PICO_SDK_PATH if your SDK lives elsewhere.
set(PICO_PLATFORM rp2350 CACHE STRING "Pico SDK platform")
# EggBert uses the RP2354A's 2 MB in-package flash, not Pico 2's 4 MB default.
# The final four 4 KB sectors hold settings and calibration A/B records.
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "EggBert flash size" FORCE)

# Use the requested SDK, or the extension's selected installation by default.
if (NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR
        "Pico SDK not found at '${PICO_SDK_PATH}'. Install the SDK selected by "
        "the Pico VS Code extension, or configure with -DPICO_SDK_PATH=<sdk-directory>.")
endif()

# Synchronize the cache with the helper's normal variables. With older CMake
# policies, setting a CACHE variable can otherwise resurrect a stale path.
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Pico SDK" FORCE)
if (PICO_TOOLCHAIN_PATH)
    set(PICO_TOOLCHAIN_PATH "${PICO_TOOLCHAIN_PATH}" CACHE PATH "Pico toolchain directory" FORCE)
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(eggbert_bringup C CXX ASM)
pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(EGGBERT_APP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/apps")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/apps.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_apps.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_apps.py"
        --manifest "${CMAKE_CURRENT_SOURCE_DIR}/apps.json" --output-dir "${EGGBERT_APP_OUTPUT}"
    RESULT_VARIABLE EGGBERT_APP_RESULT
    ERROR_VARIABLE EGGBERT_APP_ERROR
)
if (NOT EGGBERT_APP_RESULT EQUAL 0)
    message(FATAL_ERROR "${EGGBERT_APP_ERROR}")
endif()
include("${EGGBERT_APP_OUTPUT}/app_sources.cmake")

add_executable(eggbert_bringup
    src/main.c
    src/lsm6dsv.c
    src/ssd1306.c
    src/sound.c
    src/settings.c
    src/idle_power.c
    src/processor_sleep.c
    src/pendulum_detector.c
    src/maze.c
    src/app.c
    src/home.c
    src/home_menu.c
    src/ui.c
    "${EGGBERT_APP_OUTPUT}/app_registry.c"
    ${EGGBERT_APP_SOURCES}
)

target_include_directories(eggbert_bringup PRIVATE include)
target_link_libraries(eggbert_bringup PRIVATE
    pico_stdlib
    hardware_gpio
    hardware_adc
    hardware_i2c
    hardware_spi
    hardware_flash
    hardware_pwm
    hardware_clocks
)

# Leave the final 16 KB outside the image: settings A/B, then calibration A/B.
pico_set_linker_script(eggbert_bringup ${CMAKE_CURRENT_LIST_DIR}/memmap_eggbert.ld)
set_property(TARGET eggbert_bringup APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/eggbert_flash_region.ld")
target_link_options(eggbert_bringup PRIVATE "LINKER:-L${CMAKE_CURRENT_LIST_DIR}")

# USB CDC gives a serial log; no physical UART wiring is assumed.
pico_enable_stdio_usb(eggbert_bringup 1)
pico_enable_stdio_uart(eggbert_bringup 0)
pico_add_extra_outputs(eggbert_bringup)
