#!/bin/sh
# Builds the Phase 0 spikes natively and as WebAssembly, then runs both.
#   spikes/build.sh [plaits|dexed|sst]...   (default: all)
# Third-party sources come from third_party/ (see third_party/fetch.sh).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP=$ROOT/third_party
OUT=$ROOT/spikes/out
mkdir -p "$OUT/native" "$OUT/wasm"
which em++ >/dev/null || { echo "em++ not found: source emsdk_env.sh"; exit 1; }

run() {  # name, sources..., flags in $FLAGS
  name=$1; shift
  echo "== $name: native"
  clang++ -std=c++17 -O2 -ffp-contract=off $FLAGS "$@" -o "$OUT/native/$name"
  "$OUT/native/$name" "$OUT/native"
  echo "== $name: wasm (node)"
  em++ -std=c++17 -O3 -msimd128 $FLAGS "$@" -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -o "$OUT/wasm/$name.js"
  node "$OUT/wasm/$name.js" "$OUT/wasm"
}

want() { [ $# -eq 0 ] || echo " $ARGS " | grep -q " $1 "; }
ARGS="$*"

if [ -z "$ARGS" ] || want plaits; then
  P=$TP/eurorack
  SRC=$(cd "$P" && ls plaits/dsp/*.cc plaits/dsp/*/*.cc stmlib/dsp/units.cc stmlib/utils/random.cc plaits/resources.cc | grep -v test | sed "s|^|$P/|")
  FLAGS="-DTEST -I$P -w" run plaits_spike "$ROOT/spikes/plaits/plaits_spike.cc" $SRC
fi

if [ -z "$ARGS" ] || want dexed; then
  D=$TP/dexed/Source
  mkdir -p "$ROOT/spikes/dexed/carts"
  unzip -o -q -j "$TP/dexed/assets/builtin_pgm.zip" SynprezFM_01.syx SynprezFM_03.syx -d "$ROOT/spikes/dexed/carts"
  SRC="$D/msfa/dx7note.cc $D/msfa/env.cc $D/msfa/exp2.cc $D/msfa/fm_core.cc $D/msfa/fm_op_kernel.cc $D/msfa/freqlut.cc $D/msfa/lfo.cc $D/msfa/pitchenv.cc $D/msfa/porta.cpp $D/msfa/sin.cc $D/engine/EngineMkI.cpp $ROOT/spikes/dexed/shim/tuning_standard.cc"
  (cd "$ROOT" && FLAGS="-I$ROOT/spikes/dexed/shim -I$D -I$D/msfa -w" run dexed_spike "$ROOT/spikes/dexed/dexed_spike.cc" $SRC)
fi

if [ -z "$ARGS" ] || want sst; then
  INC="-I$TP/sst-effects/include -I$TP/sst-basic-blocks/include -I$TP/sst-filters/include -I$TP/sst-waveshapers/include -I$TP/simde -I$TP/fmt/include -DFMT_HEADER_ONLY"
  FLAGS="-std=c++20 $INC -w" run sst_spike "$ROOT/spikes/sst/sst_spike.cc"
fi
