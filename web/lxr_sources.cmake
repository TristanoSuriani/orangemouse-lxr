# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

# The LXR firmware as a library-like set of sources, shared by the WebAssembly
# build (web/CMakeLists.txt) and the native host tests (tests/host/). Include
# it after setting LXR_ROOT; call lxr_configure(<target>) on the executable.

get_filename_component(LXR_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/port" ABSOLUTE)

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
    ${LXR_PORT_DIR}/web_arm_division.c
    ${LXR_PORT_DIR}/web_encoder.c
    ${LXR_PORT_DIR}/web_hal.c
    ${LXR_PORT_DIR}/web_io_stubs.c
    ${LXR_PORT_DIR}/web_lcd.c
    ${LXR_PORT_DIR}/web_main.c
    ${LXR_PORT_DIR}/web_sdcard.c
    ${LXR_PORT_DIR}/web_test.c
    ${LXR_PORT_DIR}/web_lxr_ui.c
)

# The LXR as a studio engine (engines/engine.h).
set(lxr_engine_sources ${LXR_ROOT}/engines/lxr/lxr_engine.c)

# -ffast-math, as in the firmware's own build. Off (the default here), the native
# and browser builds compute bit for bit the same samples (see tests/host/README.md).
option(LXR_FAST_MATH "Compile the firmware with -ffast-math" OFF)

function(lxr_configure target)
    target_include_directories(${target} PRIVATE
        ${LXR_PORT_DIR}
        ${LXR_ROOT}/engines
        ${LXR_ROOT}/generated/registry
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

    target_compile_definitions(${target} PRIVATE
        STM32F765xx
        USE_HAL_DRIVER
        # the hardware runs at 44140 Hz (its I2S prescaler cannot make 44100);
        # the studio runs at exactly 44100, and the LXR is tuned for that
        "REAL_FS=44100.f"
        "FIRMWARE_VERSION=\" ${PROJECT_VERSION}\""
    )

    target_compile_options(${target} PRIVATE
        "SHELL:-include ${LXR_PORT_DIR}/lxr_web_port.h"
        $<$<BOOL:${LXR_FAST_MATH}>:-ffast-math>
        # The firmware is written for arm-none-eabi-gcc and an MCU; these
        # warnings are about that code, not about the port.
        -Wno-implicit-function-declaration
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wno-pointer-to-int-cast
        -Wno-int-to-pointer-cast
        -w
    )
    set_source_files_properties(${lxr_port_sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-Wall;-Wno-unused-function")
endfunction()
