#!/bin/sh
# Renders the demo songs and Plaits' presets to WAV files for listening
# (PLAN.md, "By ear"): renders/<song>.wav, the whole song through the whole
# studio, as the browser plays it; renders/plaits/<preset>.wav, a C4, a C3 and
# a C5. docs/listening/ has the checklist to go with them.
#   tools/render_songs.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
cmake -S tests/host -B tests/host/build -G Ninja >/dev/null
cmake --build tests/host/build >/dev/null
mkdir -p renders
for g in trance electro synthwave dubtechno; do
  node tools/song_stream.mjs web/app/tracker/genres/$g.js > renders/$g.txt
  tests/host/build/song_render renders/$g.txt --wav renders/$g.wav | sed "s/^/$g: /"
  rm renders/$g.txt
done
PLAITS_WAVS=renders/plaits tests/host/build/plaits_test
echo "written: renders/*.wav, renders/plaits/*.wav"
