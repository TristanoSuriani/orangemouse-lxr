#!/bin/bash
# The fuzz (tests/host/fuzz.c) under AddressSanitizer, over many seeds: the
# deeper run behind tests/run.sh's 4 rounds. It found the memory errors fixed
# on 2026-10-30 (PLAN.md Phase 7.3). Takes a few minutes.
#   tests/fuzz_asan.sh [seeds] [rounds]      (default 20 seeds of 3 rounds)
set -e -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
B=tests/host/build-asan
F="-fsanitize=address -fno-omit-frame-pointer -g"
cmake -S tests/host -B $B -G Ninja -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" >/dev/null
cmake --build $B --target fuzz >/dev/null
fail=0
for seed in $(seq 1 "${1:-20}"); do
  if out=$($B/fuzz "${2:-3}" "$seed" 2>&1); then echo "ok: seed $seed"
  else echo "FAIL: seed $seed"; echo "$out" | grep -E 'FAIL|ERROR|SUMMARY|^    #[0-4] ' | head -12; fail=1; fi
done
[ $fail -eq 0 ] && echo "ALL PASSED" || { echo "FAILED"; exit 1; }
