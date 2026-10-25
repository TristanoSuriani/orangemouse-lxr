# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)

# The LXR firmware built natively for the host (macOS, Linux), with the same
# sources and port as the browser build (web/lxr_sources.cmake), so tests can
# render audio without a browser.
#
#   cmake -S tests/host -B tests/host/build -G Ninja
#   cmake --build tests/host/build
#   tests/host/build/lxr_render --wav out.wav

get_filename_component(LXR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
project(LXR_Host LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug or Release)" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS_RELEASE "-O3")

include(${LXR_ROOT}/web/lxr_sources.cmake)

# The firmware once, as an object library both tools link.
add_library(lxr_firmware OBJECT ${lxr_core_sources} ${lxr_fatfs_sources} ${lxr_port_sources} ${lxr_engine_sources})
lxr_configure(lxr_firmware)
target_compile_options(lxr_firmware PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/host_port.h"
    # the browser build has no fused multiply-add; without this, clang fuses on
    # ARM and the samples differ from the browser's (see spikes/README.md)
    -ffp-contract=off
    # Apple clang predefines __weak for Objective-C, which hides the HAL's
    # weak-symbol macro (FatFs' default get_fattime would clash with fatfs.c)
    -U__weak
)
target_compile_definitions(lxr_firmware PRIVATE "__weak=__attribute__((weak))")

# musl's sinf, cosf, expf, exp, logf, exp2f, powf and sin, as Emscripten links them into the
# browser build (musl_rename.h). Found through emcc; without Emscripten the
# host build uses the system libm and differs from the browser by a few LSB.
find_program(LXR_EMCC emcc HINTS "$ENV{EMSDK}/upstream/emscripten")
if(EMSCRIPTEN)
    # built with Emscripten (the parity build), the system libm already is musl
elseif(LXR_EMCC)
    get_filename_component(_emcc "${LXR_EMCC}" REALPATH)
    get_filename_component(_emroot "${_emcc}" DIRECTORY)
    set(MUSL_DIR "${_emroot}/system/lib/libc/musl")
endif()
if(EMSCRIPTEN)
elseif(MUSL_DIR AND EXISTS "${MUSL_DIR}/src/math/sinf.c")
    set(musl_math_sources)
    foreach(f __math_divzerof __math_invalidf __math_oflow __math_oflowf __math_uflow __math_uflowf
              __math_xflow __math_xflowf __rem_pio2_large __rem_pio2f __sindf __cosdf sinf cosf
              exp_data exp exp2f_data expf exp2f logf_data logf powf_data powf
              __rem_pio2 __sin __cos sin)
        list(APPEND musl_math_sources "${MUSL_DIR}/src/math/${f}.c")
    endforeach()
    add_library(musl_math OBJECT ${musl_math_sources})
    target_include_directories(musl_math PRIVATE musl_shim "${MUSL_DIR}/src/internal" "${MUSL_DIR}/src/math")
    target_compile_options(musl_math PRIVATE "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/musl_rename.h" -ffp-contract=off -w)
    set(musl_math_definition LXR_HOST_MUSL_MATH)
    set(musl_objects $<TARGET_OBJECTS:musl_math>)
    message(STATUS "libm: musl from ${MUSL_DIR}")
else()
    message(WARNING "Emscripten not found: the host build uses the system libm and may differ from the browser by a few LSB")
endif()

target_compile_definitions(lxr_firmware PRIVATE ${musl_math_definition})

# The studio (router, mixer) and Orange Mouse VA.
add_library(om_core OBJECT
    ${LXR_ROOT}/studio/studio.c
    ${LXR_ROOT}/studio/tracker.c
    ${LXR_ROOT}/engines/orangemouse-va/VaEngine.c
    ${LXR_ROOT}/engines/orangemouse-va/VaPresets.c
    ${LXR_ROOT}/engines/orangemouse-va/va_engine.c
)
target_include_directories(om_core PUBLIC ${LXR_ROOT}/studio ${LXR_ROOT}/engines ${LXR_ROOT}/engines/orangemouse-va ${LXR_ROOT}/generated/registry)
target_compile_options(om_core PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/om_math.h" -ffp-contract=off -Wall)
target_compile_definitions(om_core PRIVATE ${musl_math_definition})

set(lxr_tools lxr_render)
if(NOT EMSCRIPTEN)
    list(APPEND lxr_tools lxr_sweep)   # it forks
endif()
foreach(tool ${lxr_tools})
    add_executable(${tool} ${tool}.c $<TARGET_OBJECTS:lxr_firmware> ${musl_objects})
    target_include_directories(${tool} PRIVATE ${LXR_ROOT}/engines)
    target_link_libraries(${tool} PRIVATE m)
endforeach()
add_executable(studio_render studio_render.c $<TARGET_OBJECTS:lxr_firmware> $<TARGET_OBJECTS:om_core> ${musl_objects})
add_executable(studio_test studio_test.c $<TARGET_OBJECTS:om_core> ${musl_objects})
add_executable(tracker_test tracker_test.c $<TARGET_OBJECTS:om_core> ${musl_objects})
foreach(tool studio_render studio_test tracker_test)
    target_include_directories(${tool} PRIVATE ${LXR_ROOT}/studio ${LXR_ROOT}/engines)
    target_link_libraries(${tool} PRIVATE m)
endforeach()

# The parity build: the same tools as node programs, run by tests/run.sh to
# show the browser computes what the native tests checked.
#   emcmake cmake -S tests/host -B tests/host/build-wasm -G Ninja
if(EMSCRIPTEN)
    foreach(tool ${lxr_tools} studio_render studio_test tracker_test)
        target_link_options(${tool} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB -sEXIT_RUNTIME=1)
    endforeach()
endif()
