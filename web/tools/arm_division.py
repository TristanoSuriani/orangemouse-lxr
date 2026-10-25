#!/usr/bin/env python3
# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
"""Give the integer divisions in lxr.wasm Cortex-M7 semantics.

WebAssembly traps on division by zero (and on INT_MIN / -1), the Cortex-M7
returns 0 instead and the firmware depends on it. This replaces every
i32/i64 div/rem instruction, outside the helper functions themselves, by a
call to the matching web_arm_* helper from port/web_arm_division.c, then lets
wasm-opt inline the helpers again.

    arm_division.py [--binaryen <dir of wasm-dis, wasm-as, wasm-opt>] [--strip] lxr.wasm
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

OPS = {
    f"i{bits}.{op}_{sign}": f"web_arm_{op}_{sign}{bits}"
    for bits in (32, 64)
    for op in ("div", "rem")
    for sign in ("s", "u")
}
OP_RE = re.compile(r"\((i(?:32|64)\.(?:div|rem)_[su])(?![\w.])")
FUNC_RE = re.compile(r"^ \(func \$(\S+)")
EXPORT_RE = re.compile(r'^ \(export "([^"]+)" \(func \$(\S+)\)\)')
# The WebAssembly features Emscripten targets by default. The text format
# doesn't record them, so name them for reassembling and optimising.
FEATURES = [
    "--enable-bulk-memory",
    "--enable-bulk-memory-opt",
    "--enable-call-indirect-overlong",
    "--enable-multivalue",
    "--enable-mutable-globals",
    "--enable-nontrapping-float-to-int",
    "--enable-reference-types",
    "--enable-sign-ext",
    "--enable-simd",   # the send effects (sst-effects, via SIMDE)
]


def tool(binaryen, name):
    return os.path.join(binaryen, name) if binaryen else name


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binaryen", default="", help="directory of the Binaryen tools")
    parser.add_argument("--strip", action="store_true", help="drop the function names")
    parser.add_argument("wasm")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        wat = os.path.join(tmp, "lxr.wat")
        subprocess.run([tool(args.binaryen, "wasm-dis"), args.wasm, "-o", wat], check=True)
        with open(wat, encoding="utf-8") as f:
            lines = f.read().split("\n")

        # internal names of the helper functions
        helpers = {}
        for line in lines:
            m = EXPORT_RE.match(line)
            if m and m.group(1) in OPS.values():
                helpers[m.group(1)] = m.group(2)
        missing = sorted(set(OPS.values()) - set(helpers))
        if missing:
            sys.exit(f"arm_division.py: helper functions not exported: {', '.join(missing)}")
        helper_names = set(helpers.values())

        replaced = 0
        current = None
        for i, line in enumerate(lines):
            m = FUNC_RE.match(line)
            if m:
                current = m.group(1)
            if current in helper_names or "." not in line:
                continue

            def call(m):
                return f"(call ${helpers[OPS[m.group(1)]]}"

            new, n = OP_RE.subn(call, line)
            if n:
                lines[i] = new
                replaced += n

        with open(wat, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
        rewritten = os.path.join(tmp, "lxr.wasm")
        subprocess.run([tool(args.binaryen, "wasm-as"), wat, *FEATURES, "-o", rewritten], check=True)
        subprocess.run([tool(args.binaryen, "wasm-opt"), rewritten, *FEATURES, "-O3", "-o", args.wasm]
                       + (["--strip-debug"] if args.strip else []), check=True)

    print(f"arm_division.py: {replaced} divisions given Cortex-M semantics")


if __name__ == "__main__":
    main()
