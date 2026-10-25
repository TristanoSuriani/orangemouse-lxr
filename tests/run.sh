#!/bin/sh
# Orange Mouse tests (PLAN.md, "Testing"). Run from anywhere:
#   tests/run.sh                    check everything against tests/golden/
#   UPDATE_GOLDEN=1 tests/run.sh    accept the current sound and CPU as the new golden files
# Needs cmake, ninja and a C compiler; the WebAssembly parity check also
# needs Emscripten (source emsdk_env.sh) and node.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=tests/host/out
GOLDEN=tests/golden
mkdir -p "$OUT" "$GOLDEN"
fail=0

check() {  # name, file: compare with the golden copy, or replace it when updating
  if [ -n "$UPDATE_GOLDEN" ]; then
    cp "$2" "$GOLDEN/$1"; echo "   updated $GOLDEN/$1"
  elif ! diff -u "$GOLDEN/$1" "$2" > "$OUT/$1.diff"; then
    echo "   FAIL: $1 differs from $GOLDEN/$1 ($(grep -c '^+[^+]' "$OUT/$1.diff") lines):"
    grep '^[-+][^-+]' "$OUT/$1.diff" | head -20
    fail=1
  else
    echo "   ok: $1"
  fi
}

echo "== host build"
cmake -S tests/host -B tests/host/build -G Ninja >/dev/null
cmake --build tests/host/build >/dev/null

echo "== LXR demo groove"
tests/host/build/lxr_render --raw "$OUT/demo.f32" --wav "$OUT/demo.wav" > "$OUT/demo.txt"
grep '^hash' "$OUT/demo.txt" > "$OUT/lxr_demo.txt"
check lxr_demo.txt "$OUT/lxr_demo.txt"

echo "== LXR parameter sweep (golden renders and health)"
if ! tests/host/build/lxr_sweep > "$OUT/lxr_params.txt"; then
  echo "   FAIL: a render crashed or produced NaN/Inf:"
  grep -v 'bad=0$' "$OUT/lxr_params.txt" | head
  fail=1
fi
check lxr_params.txt "$OUT/lxr_params.txt"

echo "== LXR CPU (best of 3, +25 % allowed)"
best=$(for i in 1 2 3; do tests/host/build/lxr_render | sed -n 's/.*, \([0-9.]*\) us\/block.*/\1/p'; done | sort -n | head -1)
echo "   $best us per 64-sample block"
if [ -n "$UPDATE_GOLDEN" ]; then
  echo "$best" > "$GOLDEN/lxr_cpu.txt"; echo "   updated $GOLDEN/lxr_cpu.txt"
elif [ -f "$GOLDEN/lxr_cpu.txt" ]; then
  if awk -v now="$best" -v was="$(cat "$GOLDEN/lxr_cpu.txt")" 'BEGIN { exit !(now > was * 1.25) }'; then
    echo "   FAIL: slower than $(cat "$GOLDEN/lxr_cpu.txt") us + 25 %"; fail=1
  else
    echo "   ok: baseline $(cat "$GOLDEN/lxr_cpu.txt") us"
  fi
fi

if command -v emcmake >/dev/null && command -v node >/dev/null; then
  echo "== browser build and parity"
  emcmake cmake -S web -B web/build -G Ninja >/dev/null 2>&1
  cmake --build web/build >/dev/null
  node tests/host/parity.mjs "$OUT/demo.f32" | sed 's/^/   /' || fail=1
else
  echo "== parity skipped: Emscripten or node not found"
fi

[ $fail -eq 0 ] && echo "ALL PASSED" || { echo "FAILED"; exit 1; }
