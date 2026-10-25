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

project(LXR_V2_Web VERSION ${LXR_VERSION} LANGUAGES C CXX)

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
set(CMAKE_CXX_FLAGS_DEBUG   "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include(${CMAKE_CURRENT_LIST_DIR}/lxr_sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../studio/fx_sources.cmake)

# Two modules, both standalone without Emscripten's JavaScript runtime: the
# AudioWorklet instantiates them directly.
#   studio.wasm  the studio (studio/): the LXR and Orange Mouse VA, mixed,
#                with the send effects; the app runs it (app/studio-worklet.js)
#   lxr.wasm     the LXR firmware alone, as before; tests/host/parity.mjs
#                compares it with the native build
# the send effects, in C++, apart from the firmware's forced-include header
add_library(om_fx OBJECT ${om_fx_sources})
om_fx_configure(om_fx)
include(${CMAKE_CURRENT_LIST_DIR}/../engines/dexed/dexed_sources.cmake)
add_library(om_dexed OBJECT ${om_dexed_sources})
om_dexed_configure(om_dexed)

add_executable(lxr ${lxr_core_sources} ${lxr_fatfs_sources} ${lxr_port_sources})
lxr_configure(lxr)

add_executable(studio
    ${lxr_core_sources} ${lxr_fatfs_sources} ${lxr_port_sources} ${lxr_engine_sources}
    ${LXR_ROOT}/studio/studio.c
    ${LXR_ROOT}/studio/tracker.c
    ${LXR_ROOT}/engines/orangemouse-va/VaEngine.c
    ${LXR_ROOT}/engines/orangemouse-va/VaPresets.c
    ${LXR_ROOT}/engines/orangemouse-va/va_engine.c
    port/web_studio.c
    $<TARGET_OBJECTS:om_fx>
    $<TARGET_OBJECTS:om_dexed>
)
lxr_configure(studio)
target_include_directories(studio PRIVATE ${LXR_ROOT}/studio ${LXR_ROOT}/engines/orangemouse-va ${LXR_ROOT}/engines/dexed)

# Cortex-M7 semantics for integer division by zero, see port/web_arm_division.c.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(LXR_EMSCRIPTEN_ROOT "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(LXR_WASM_OPT wasm-opt
    HINTS "${LXR_EMSCRIPTEN_ROOT}/../bin" "${EMSCRIPTEN_ROOT_PATH}/../bin"
    REQUIRED
)
get_filename_component(LXR_BINARYEN_DIR "${LXR_WASM_OPT}" DIRECTORY)

foreach(module lxr studio)
    set_target_properties(${module} PROPERTIES
        SUFFIX ".wasm"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/app"
    )
    target_link_options(${module} PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sFILESYSTEM=0
        -sINITIAL_MEMORY=32MB
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1MB
        -sERROR_ON_UNDEFINED_SYMBOLS=1
        -sEXPORTED_FUNCTIONS=_malloc,_free
    )
    add_custom_command(TARGET ${module} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/arm_division.py
                --binaryen ${LXR_BINARYEN_DIR}
                $<$<NOT:$<CONFIG:Debug>>:--strip>
                $<TARGET_FILE:${module}>
        VERBATIM
    )
endforeach()
