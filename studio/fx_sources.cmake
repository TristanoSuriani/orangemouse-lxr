# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
# The studio's send effects (studio/fx.cpp) with Surge XT's sst-effects, and
# its channel filters (studio/filter.cpp) with sst-filters and waveshapers
# (studio/shaper.cpp) with sst-waveshapers, shared
# by the browser build (web/CMakeLists.txt) and the host tests (tests/host/).
# The sst libraries are header-only, fetched by third_party/fetch.sh.
get_filename_component(OM_FX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(OM_THIRD_PARTY "${OM_FX_ROOT}/third_party")
if(NOT EXISTS "${OM_THIRD_PARTY}/sst-effects/include")
    message(FATAL_ERROR "sst-effects not found: run third_party/fetch.sh")
endif()
set(om_fx_sources ${OM_FX_ROOT}/studio/fx.cpp ${OM_FX_ROOT}/studio/filter.cpp ${OM_FX_ROOT}/studio/shaper.cpp)

# Clouds (studio/clouds.cpp, a send effect type): Mutable Instruments' DSP from
# third_party/eurorack, apart from the sst sources (stmlib's macros). stmlib's
# units.cc and random.cc come with Plaits (engines/plaits/plaits_sources.cmake):
# link both.
set(OM_CLOUDS_EURORACK "${OM_THIRD_PARTY}/eurorack")
file(GLOB om_clouds_dsp CONFIGURE_DEPENDS "${OM_CLOUDS_EURORACK}/clouds/dsp/*.cc" "${OM_CLOUDS_EURORACK}/clouds/dsp/pvoc/*.cc")
set(om_clouds_sources ${OM_FX_ROOT}/studio/clouds.cpp ${om_clouds_dsp} ${OM_CLOUDS_EURORACK}/clouds/resources.cc
    ${OM_CLOUDS_EURORACK}/stmlib/dsp/atan.cc)   # the phase vocoder's; Plaits has no use for it

function(om_clouds_configure target)
    target_include_directories(${target} PRIVATE ${OM_CLOUDS_EURORACK} ${OM_FX_ROOT}/studio ${OM_FX_ROOT}/engines)
    # TEST: stmlib's portable code paths (no ARM intrinsics), as for Plaits
    target_compile_definitions(${target} PRIVATE TEST)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -w)
endfunction()

function(om_fx_configure target)
    target_include_directories(${target} PRIVATE
        ${OM_FX_ROOT}/studio
        ${OM_FX_ROOT}/engines
        ${OM_FX_ROOT}/generated/registry
        ${OM_THIRD_PARTY}/sst-effects/include
        ${OM_THIRD_PARTY}/sst-basic-blocks/include
        ${OM_THIRD_PARTY}/sst-filters/include
        ${OM_THIRD_PARTY}/sst-waveshapers/include
        ${OM_THIRD_PARTY}/simde
        ${OM_THIRD_PARTY}/fmt/include
    )
    target_compile_definitions(${target} PRIVATE FMT_HEADER_ONLY)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    # third-party headers: their warnings are not ours
    set_source_files_properties(${om_fx_sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "-w")
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE -msimd128)
    endif()
endfunction()
