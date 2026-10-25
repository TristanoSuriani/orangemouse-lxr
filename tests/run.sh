#!/bin/sh
# Orange Mouse tests (PLAN.md, "Testing"). Run from anywhere:
#   tests/run.sh
# Needs cmake, ninja and a C compiler; the WebAssembly parity check also
# needs Emscripten (source emsdk_env.sh) and node.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=tests/host/out
mkdir -p "$OUT"
fail=0

echo "== host build"
cmake -S tests/host -B tests/host/build -G Ninja >/dev/null
cmake --build tests/host/build >/dev/null

echo "== LXR demo groove"
tests/host/build/lxr_render --raw "$OUT/demo.f32" --wav "$OUT/demo.wav" | tee "$OUT/demo.txt"

if command -v emcmake >/dev/null && command -v node >/dev/null; then
  echo "== browser build and parity"
  emcmake cmake -S web -B web/build -G Ninja >/dev/null 2>&1
  cmake --build web/build >/dev/null
  node tests/host/parity.mjs "$OUT/demo.f32" || fail=1
else
  echo "== parity skipped: Emscripten or node not found"
fi

[ $fail -eq 0 ] && echo "ALL PASSED" || { echo "FAILED"; exit 1; }
