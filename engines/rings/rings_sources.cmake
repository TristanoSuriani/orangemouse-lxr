# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
# Rings in the studio (engines/rings/): Mutable Instruments' DSP from
# third_party/eurorack (third_party/fetch.sh), its resonator part (not the
# string synth easter egg). stmlib's .cc files come with Plaits
# (plaits_sources.cmake): link both. Shared by the browser build and the
# host tests.
get_filename_component(OM_RINGS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(OM_RINGS_EURORACK "${OM_RINGS_ROOT}/third_party/eurorack")
if(NOT EXISTS "${OM_RINGS_EURORACK}/rings/dsp/part.cc")
    message(FATAL_ERROR "Rings not found: run third_party/fetch.sh")
endif()
set(om_rings_sources
    ${OM_RINGS_ROOT}/engines/rings/rings_engine.cpp
    ${OM_RINGS_EURORACK}/rings/dsp/part.cc
    ${OM_RINGS_EURORACK}/rings/dsp/resonator.cc
    ${OM_RINGS_EURORACK}/rings/dsp/string.cc
    ${OM_RINGS_EURORACK}/rings/dsp/fm_voice.cc
    ${OM_RINGS_EURORACK}/rings/resources.cc
)

function(om_rings_configure target)
    target_include_directories(${target} PRIVATE
        ${OM_RINGS_EURORACK}
        ${OM_RINGS_ROOT}/engines/rings
        ${OM_RINGS_ROOT}/engines
        ${OM_RINGS_ROOT}/generated/registry
    )
    # TEST: stmlib's portable code paths (no ARM intrinsics), as for Plaits
    target_compile_definitions(${target} PRIVATE TEST)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -w)
endfunction()
