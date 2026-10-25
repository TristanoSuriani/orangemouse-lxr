# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)

# LXR V2 firmware as a browser app: the firmware sources compiled to
# WebAssembly with Emscripten, running in an AudioWorklet.
#
#   emcmake cmake -S web -B web/build -G Ninja
#   cmake --build web/build
#
# This writes web/app/lxr.wasm; serve web/app/ over HTTP. See web/README.md.

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${LXR_ROOT}/CMakeLists.txt" lxr_top_cmake)
string(REGEX MATCH "project\\(LXR_V2_STM32F765[ \t\r\n]+VERSION ([0-9.]+)" _ "${lxr_top_cmake}")
set(LXR_VERSION "${CMAKE_MATCH_1}")

project(LXR_V2_Web VERSION ${LXR_VERSION} LANGUAGES C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure the web build with Emscripten: emcmake cmake -S web -B web/build -G Ninja")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS_DEBUG   "-O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-O3")

# Firmware sources that run unchanged. The hardware-specific files left out
# here are replaced by the port (port/): startup, clocks and interrupts
# (main.c, stm32f7xx_it.c, ...), the LCD and encoder drivers, the I2S codec
# driver, USB-MIDI and the SD card driver.
file(GLOB_RECURSE lxr_core_sources CONFIGURE_DEPENDS "${LXR_ROOT}/Core/Src/*.c")
list(FILTER lxr_core_sources EXCLUDE REGEX
    "/Core/Src/(main|stm32f7xx_it|stm32f7xx_hal_msp|system_stm32f7xx|syscalls|sysmem|delay|flash_if|lcd|encoder)\\.c$")
list(FILTER lxr_core_sources EXCLUDE REGEX
    "/Soundengine/(AudioCodecManager|cs4344_cs5343|USB/usb_manager)\\.c$")

set(lxr_fatfs_sources
    ${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/diskio.c
    ${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff.c
    ${LXR_ROOT}/Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ${LXR_ROOT}/FATFS/App/fatfs.c
)

set(lxr_port_sources
    port/web_arm_division.c
    port/web_encoder.c
    port/web_hal.c
    port/web_io_stubs.c
    port/web_lcd.c
    port/web_main.c
    port/web_sdcard.c
)

add_executable(lxr ${lxr_core_sources} ${lxr_fatfs_sources} ${lxr_port_sources})

target_include_directories(lxr PRIVATE
    port
    ${LXR_ROOT}/Core/Inc
    ${LXR_ROOT}/Drivers/CMSIS/Include
    ${LXR_ROOT}/Drivers/CMSIS/Device/ST/STM32F7xx/Include
    ${LXR_ROOT}/Drivers/STM32F7xx_HAL_Driver/Inc
    ${LXR_ROOT}/Middlewares/Third_Party/FatFs/src
    ${LXR_ROOT}/FATFS/App
    ${LXR_ROOT}/FATFS/Target
    ${LXR_ROOT}/Middlewares/JS/STM32_USB_Device_Library/Core/Inc
    ${LXR_ROOT}/Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Inc
    ${LXR_ROOT}/USB_DEVICE_MIDI/App
    ${LXR_ROOT}/USB_DEVICE_MIDI/Target
)

target_compile_definitions(lxr PRIVATE
    STM32F765xx
    USE_HAL_DRIVER
    "FIRMWARE_VERSION=\" ${PROJECT_VERSION}\""
)

target_compile_options(lxr PRIVATE
    -include ${CMAKE_CURRENT_LIST_DIR}/port/lxr_web_port.h
    -ffast-math
    # The firmware is written for arm-none-eabi-gcc and an MCU; these
    # warnings are about that code, not about the port.
    -Wno-implicit-function-declaration
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -w
)
set_source_files_properties(${lxr_port_sources} PROPERTIES COMPILE_OPTIONS "-Wall;-Wno-unused-function")

# A standalone module without Emscripten's JavaScript runtime: the
# AudioWorklet instantiates it directly (see app/lxr-worklet.js).
set_target_properties(lxr PROPERTIES
    SUFFIX ".wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/app"
)
target_link_options(lxr PRIVATE
    --no-entry
    -sSTANDALONE_WASM=1
    -sFILESYSTEM=0
    -sINITIAL_MEMORY=32MB
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1MB
    -sERROR_ON_UNDEFINED_SYMBOLS=1
    -sEXPORTED_FUNCTIONS=_malloc,_free
)

# Cortex-M7 semantics for integer division by zero, see port/web_arm_division.c.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(LXR_EMSCRIPTEN_ROOT "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(LXR_WASM_OPT wasm-opt
    HINTS "${LXR_EMSCRIPTEN_ROOT}/../bin" "${EMSCRIPTEN_ROOT_PATH}/../bin"
    REQUIRED
)
get_filename_component(LXR_BINARYEN_DIR "${LXR_WASM_OPT}" DIRECTORY)
add_custom_command(TARGET lxr POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/arm_division.py
            --binaryen ${LXR_BINARYEN_DIR}
            $<$<NOT:$<CONFIG:Debug>>:--strip>
            $<TARGET_FILE:lxr>
    VERBATIM
)
