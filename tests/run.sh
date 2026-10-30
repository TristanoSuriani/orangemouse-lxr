#!/bin/bash
# Orange Mouse tests (PLAN.md, "Testing"). Run from anywhere:
#   tests/run.sh                    check everything against tests/golden/
#   UPDATE_GOLDEN=1 tests/run.sh    accept the current sound and CPU as the new golden files
# Needs cmake, ninja and a C compiler; the WebAssembly parity checks also
# need Emscripten (source emsdk_env.sh) and node.
set -e -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=tests/host/out
GOLDEN=tests/golden
BIN=tests/host/build
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

cpu() {  # name, command...: best of 3 us/block against the golden budget (+25 %)
  name=$1; shift
  best=$(for i in 1 2 3; do "$@" | sed -n 's/.*, \([0-9.]*\) us\/block.*/\1/p'; done | sort -n | head -1)
  if [ -n "$UPDATE_GOLDEN" ]; then
    echo "$best" > "$GOLDEN/$name"; echo "   $best us per 64-frame block; updated $GOLDEN/$name"
  elif [ -f "$GOLDEN/$name" ]; then
    was=$(cat "$GOLDEN/$name")
    if awk -v now="$best" -v was="$was" 'BEGIN { exit !(now > was * 1.25) }'; then
      echo "   FAIL: $best us per 64-frame block, more than $was us + 25 %"; fail=1
    else
      echo "   ok: $best us per 64-frame block (budget $was us + 25 %)"
    fi
  fi
}

echo "== host build"
cmake -S tests/host -B $BIN -G Ninja >/dev/null
cmake --build $BIN >/dev/null

echo "== the app's JavaScript parses"
for f in web/app/*.js web/app/*/*.js tests/*.mjs tests/browser/*.mjs; do node --check "$f" || fail=1; done
echo "   ok"

echo "== parameter registry"
{ python3 -I tools/lxr_registry.py --check && python3 -I tools/registry.py --check && node tests/registry_test.mjs; } \
  | sed 's/^/   /' || fail=1

echo "== songs can't be lost (the backup folder, the export reminder)"
node tests/backup_test.mjs | sed 's/^/   /' || fail=1

echo "== studio, tracker, clock, Dexed and Plaits tests"
$BIN/studio_test | sed 's/^/   /' || fail=1
$BIN/tracker_test | sed 's/^/   /' || fail=1
$BIN/lxr_sync_test | sed 's/^/   /' || fail=1
$BIN/dexed_test | sed 's/^/   /' || fail=1
$BIN/plaits_test | sed 's/^/   /' || fail=1

echo "== LXR demo groove"
$BIN/lxr_render --raw "$OUT/demo.f32" --wav "$OUT/demo.wav" | grep '^hash' > "$OUT/lxr_demo.txt"
check lxr_demo.txt "$OUT/lxr_demo.txt"

echo "== LXR parameter sweep (golden renders and health)"
if ! $BIN/lxr_sweep > "$OUT/lxr_params.txt"; then
  echo "   FAIL: a render crashed or produced NaN/Inf:"
  grep -v 'bad=0$' "$OUT/lxr_params.txt" | head
  fail=1
fi
check lxr_params.txt "$OUT/lxr_params.txt"

echo "== studio demo song (LXR, Orange Mouse VA, Dexed, Plaits, the send effects)"
$BIN/studio_render --wav "$OUT/studio.wav" | grep '^hash' > "$OUT/studio_demo.txt"
check studio_demo.txt "$OUT/studio_demo.txt"

echo "== the genre songs (whole, through the whole studio; PLAN.md Phase 6)"
mkdir -p "$OUT/songs"
: > "$OUT/songs.txt"
for g in electro synthwave dubtechno; do
  node tools/song_stream.mjs web/app/tracker/genres/$g.js > "$OUT/songs/$g.txt"
  if ! $BIN/song_render "$OUT/songs/$g.txt" > "$OUT/songs/$g.out"; then
    echo "   FAIL: $g:"; grep -E 'silent|not finite' "$OUT/songs/$g.out" | sed 's/^/     /'; fail=1
  fi
  grep -E '^(health|hash)' "$OUT/songs/$g.out" | sed "s/^/$g /" >> "$OUT/songs.txt"
done
check songs.txt "$OUT/songs.txt"

echo "== CPU (best of 3)"
cpu lxr_cpu.txt $BIN/lxr_render
cpu studio_cpu.txt $BIN/studio_render
cpu song_cpu.txt $BIN/song_render "$OUT/songs/electro.txt"

if command -v emcmake >/dev/null && command -v node >/dev/null; then
  echo "== WebAssembly parity"
  emcmake cmake -S web -B web/build -G Ninja >/dev/null 2>&1
  cmake --build web/build >/dev/null
  node tests/host/parity.mjs "$OUT/demo.f32" | sed 's/^/   web\/app\/lxr.wasm: /' || fail=1
  emcmake cmake -S tests/host -B tests/host/build-wasm -G Ninja >/dev/null 2>&1
  cmake --build tests/host/build-wasm >/dev/null
  node tests/host/build-wasm/studio_test.js | sed 's/^/   wasm: /' || fail=1
  node tests/host/build-wasm/tracker_test.js | sed 's/^/   wasm: /' || fail=1
  node tests/host/build-wasm/dexed_test.js | sed 's/^/   wasm: /' || fail=1
  node tests/host/build-wasm/plaits_test.js | sed 's/^/   wasm: /' || fail=1
  # each: a tool, and its arguments (the electro song: the whole studio, every engine)
  for run in "lxr_render" "studio_render" "song_render $OUT/songs/electro.txt"; do
    set -- $run
    tool=$1; shift
    native=$($BIN/$tool "$@" | grep '^hash')
    wasm=$(node tests/host/build-wasm/$tool.js "$@" | grep '^hash')
    if [ "$native" = "$wasm" ]; then echo "   ok: $tool, native and wasm $native"
    else echo "   FAIL: $tool native $native, wasm $wasm"; fail=1; fi
  done
  node tests/studio_wasm_smoke.mjs | sed 's/^/   /' || fail=1
else
  echo "== WebAssembly parity skipped: Emscripten or node not found"
fi

CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
if [ -x "$CHROME" ] && [ -f web/app/studio.wasm ]; then
  echo "== the studio in headless Chrome"
  CHROME="$CHROME" node tests/browser/smoke.mjs || fail=1
else
  echo "== browser test skipped: no Chrome (set CHROME) or no web/app/studio.wasm"
fi

[ $fail -eq 0 ] && echo "ALL PASSED" || { echo "FAILED"; exit 1; }
