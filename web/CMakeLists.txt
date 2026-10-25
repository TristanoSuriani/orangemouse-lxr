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

include(${CMAKE_CURRENT_LIST_DIR}/lxr_sources.cmake)

add_executable(lxr ${lxr_core_sources} ${lxr_fatfs_sources} ${lxr_port_sources})
lxr_configure(lxr)

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
