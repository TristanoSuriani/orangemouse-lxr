# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
# Dexed in the studio (engines/dexed/): its DX7 engine (msfa, EngineMkI) from
# third_party/dexed (third_party/fetch.sh), three shims for what needs JUCE,
# and its built-in cartridges as a C table generated into the build tree.
# Shared by the browser build (web/CMakeLists.txt) and the host tests.
get_filename_component(OM_DEXED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(OM_DEXED_SRC "${OM_DEXED_ROOT}/third_party/dexed/Source")
if(NOT EXISTS "${OM_DEXED_SRC}/msfa/dx7note.cc")
    message(FATAL_ERROR "Dexed not found: run third_party/fetch.sh")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(OM_DEXED_CARTS "${CMAKE_BINARY_DIR}/generated/dexed_carts.c")
add_custom_command(
    OUTPUT ${OM_DEXED_CARTS}
    COMMAND ${Python3_EXECUTABLE} -I ${OM_DEXED_ROOT}/tools/dexed_carts.py
            ${OM_DEXED_ROOT}/third_party/dexed/assets/builtin_pgm.zip ${OM_DEXED_CARTS}
    DEPENDS ${OM_DEXED_ROOT}/tools/dexed_carts.py ${OM_DEXED_ROOT}/third_party/dexed/assets/builtin_pgm.zip
    COMMENT "Dexed's cartridges"
    VERBATIM
)
set(om_dexed_sources
    ${OM_DEXED_ROOT}/engines/dexed/dexed_engine.cpp
    ${OM_DEXED_ROOT}/engines/dexed/shim/tuning_standard.cc
    ${OM_DEXED_SRC}/msfa/dx7note.cc
    ${OM_DEXED_SRC}/msfa/env.cc
    ${OM_DEXED_SRC}/msfa/exp2.cc
    ${OM_DEXED_SRC}/msfa/fm_core.cc
    ${OM_DEXED_SRC}/msfa/fm_op_kernel.cc
    ${OM_DEXED_SRC}/msfa/freqlut.cc
    ${OM_DEXED_SRC}/msfa/lfo.cc
    ${OM_DEXED_SRC}/msfa/pitchenv.cc
    ${OM_DEXED_SRC}/msfa/porta.cpp
    ${OM_DEXED_SRC}/msfa/sin.cc
    ${OM_DEXED_SRC}/engine/EngineMkI.cpp
    ${OM_DEXED_CARTS}
)

function(om_dexed_configure target)
    target_include_directories(${target} PRIVATE
        ${OM_DEXED_ROOT}/engines/dexed/shim
        ${OM_DEXED_SRC}
        ${OM_DEXED_SRC}/msfa
        ${OM_DEXED_ROOT}/engines/dexed
        ${OM_DEXED_ROOT}/engines
        ${OM_DEXED_ROOT}/generated/registry
    )
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # Dexed's own sources: their warnings are not ours
    target_compile_options(${target} PRIVATE -w)
endfunction()
