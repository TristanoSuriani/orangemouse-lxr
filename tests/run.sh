#!/bin/bash
# Orange Mouse tests (PLAN.md, "Testing"). Run from anywhere:
#   tests/run.sh                                 check everything against tests/golden/
#   WHY="..." UPDATE_GOLDEN=1 tests/run.sh       accept the current sound and screens as golden
#   UPDATE_CPU=1 tests/run.sh                    accept the current CPU times as the budgets
# Re-baselining is deliberate (PLAN.md Phase 7.5): UPDATE_GOLDEN needs a WHY,
# and appends to tests/golden/CHANGES.md which renders changed and why; those
# are the ones to listen to. tools/hooks/pre-commit refuses a commit that
# changes a golden file without that entry.
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
if [ -n "$UPDATE_GOLDEN" ] && [ -z "$WHY" ]; then
  echo "UPDATE_GOLDEN needs a reason: WHY=\"what changed the sound, on purpose\" UPDATE_GOLDEN=1 tests/run.sh"
  exit 2
fi
CHANGES="$OUT/golden_changes.txt"   # this run's changed golden files, for tests/golden/CHANGES.md
: > "$CHANGES"

check() {  # name, file: compare with the golden copy, or replace it when updating
  if [ -n "$UPDATE_GOLDEN" ]; then
    if cmp -s "$2" "$GOLDEN/$1"; then echo "   ok: $1 (unchanged)"; return; fi
    what=$(python3 -I tools/golden_changes.py "$GOLDEN/$1" "$2")
    cp "$2" "$GOLDEN/$1"; echo "   updated $GOLDEN/$1: $what"
    echo "- \`$1\`: $what" >> "$CHANGES"
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
  if [ -n "$UPDATE_CPU" ]; then
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
node tests/playlist_test.mjs | sed 's/^/   /' || fail=1

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
for g in electro synthwave dubtechno trance; do
  node tools/song_stream.mjs web/app/tracker/genres/$g.js > "$OUT/songs/$g.txt"
  if ! $BIN/song_render "$OUT/songs/$g.txt" > "$OUT/songs/$g.out"; then
    echo "   FAIL: $g:"; grep -E 'silent|not finite' "$OUT/songs/$g.out" | sed 's/^/     /'; fail=1
  fi
  grep -E '^(health|hash)' "$OUT/songs/$g.out" | sed "s/^/$g /" >> "$OUT/songs.txt"
done
check songs.txt "$OUT/songs.txt"

echo "== the worst case: every part at its voice limit (tests/songs/stress.js; PLAN.md Phase 7.2)"
node tools/song_stream.mjs tests/songs/stress.js > "$OUT/songs/stress.txt"
if ! $BIN/song_render "$OUT/songs/stress.txt" > "$OUT/songs/stress.out"; then
  echo "   FAIL: stress:"; grep -E 'silent|not finite' "$OUT/songs/stress.out" | sed 's/^/     /'; fail=1
fi
grep -E '^(health|hash)' "$OUT/songs/stress.out" > "$OUT/stress.txt"
check stress.txt "$OUT/stress.txt"

echo "== fuzz: random parameters, note storms, songs and MIDI, then a panic (tests/host/fuzz.c)"
$BIN/fuzz 4 1 | sed 's/^/   /' || fail=1

echo "== CPU (best of 3)"
cpu lxr_cpu.txt $BIN/lxr_render
cpu studio_cpu.txt $BIN/studio_render
cpu song_cpu.txt $BIN/song_render "$OUT/songs/electro.txt"
cpu stress_cpu.txt $BIN/song_render "$OUT/songs/stress.txt"

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
  CHROME="$CHROME" node tests/browser/smoke.mjs | tee "$OUT/browser.txt" || fail=1
  grep -o 'changed: tests/golden/screens/[a-z]*.png.*' "$OUT/browser.txt" | sed 's/^changed: \(tests\/golden\/\)\{0,1\}/- `/; s/\.png/.png`/' >> "$CHANGES" || true
else
  echo "== browser test skipped: no Chrome (set CHROME) or no web/app/studio.wasm"
fi

if [ -n "$UPDATE_GOLDEN" ] && [ -s "$CHANGES" ]; then
  { [ -f "$GOLDEN/CHANGES.md" ] || printf '# Golden changes\n\nEach re-baseline: why, and which renders changed (PLAN.md Phase 7.5). Listen to those.\n'
    printf '\n## %s: %s\n\n' "$(date +%Y-%m-%d)" "$WHY"; cat "$CHANGES"; } >> "$GOLDEN/CHANGES.md"
  echo "== recorded in $GOLDEN/CHANGES.md:"; sed 's/^/   /' "$CHANGES"
fi

[ $fail -eq 0 ] && echo "ALL PASSED" || { echo "FAILED"; exit 1; }
