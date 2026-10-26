# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
# Plaits in the studio (engines/plaits/): Mutable Instruments' DSP from
# third_party/eurorack (third_party/fetch.sh), all of plaits/dsp except its
# own voice (om_plaits_voice.cc replaces it). Shared by the browser build and
# the host tests.
get_filename_component(OM_PLAITS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(OM_EURORACK "${OM_PLAITS_ROOT}/third_party/eurorack")
if(NOT EXISTS "${OM_EURORACK}/plaits/dsp/voice.cc")
    message(FATAL_ERROR "Plaits not found: run third_party/fetch.sh")
endif()
file(GLOB om_plaits_dsp CONFIGURE_DEPENDS "${OM_EURORACK}/plaits/dsp/*.cc" "${OM_EURORACK}/plaits/dsp/*/*.cc")
list(FILTER om_plaits_dsp EXCLUDE REGEX "/plaits/dsp/voice\\.cc$")
set(om_plaits_sources
    ${OM_PLAITS_ROOT}/engines/plaits/plaits_engine.cpp
    ${OM_PLAITS_ROOT}/engines/plaits/om_plaits_voice.cc
    ${OM_PLAITS_ROOT}/engines/plaits/plaits_presets.c
    ${om_plaits_dsp}
    ${OM_EURORACK}/plaits/resources.cc
    ${OM_EURORACK}/stmlib/dsp/units.cc
    ${OM_EURORACK}/stmlib/utils/random.cc
)

function(om_plaits_configure target)
    target_include_directories(${target} PRIVATE
        ${OM_EURORACK}
        ${OM_PLAITS_ROOT}/engines/plaits
        ${OM_PLAITS_ROOT}/engines
        ${OM_PLAITS_ROOT}/generated/registry
    )
    # TEST: stmlib's portable code paths (no ARM intrinsics), as in the spike
    target_compile_definitions(${target} PRIVATE TEST)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -w)
endfunction()
