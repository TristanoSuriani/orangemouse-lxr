#!/usr/bin/env python3
# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
"""The parameter registry (PLAN.md Phase 0, step 7).

Every engine's parameters are described once, in registry/<engine>.json: the
engine's enum name, the LCD short name, the full name, the page (group), the
range, the default, how to show the value, and a one-line help text. This tool
checks those files and generates, for each engine:

  generated/registry/om_registry_<engine>.h   C: the table the studio uses, plus
                                               compile-time checks that every
                                               entry sits at its enum value
  web/app/registry/<engine>.js                 the UI's labels, value display and help

    tools/registry.py            check and generate
    tools/registry.py --check    check, and fail if a generated file is out of date

registry/<engine>_help.json, when present, adds help lines (and better names)
by parameter id or by the firmware's text id: one entry helps every voice's
parameter of that name. An entry is a help string, or {"name": ..., "help": ...};
"_groups" maps page categories to readable names.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REG = ROOT / "registry"
OUT_C = ROOT / "generated" / "registry"
OUT_JS = ROOT / "web" / "app" / "registry"

SHORT_MAX = 3       # the LXR LCD's parameter labels
NAME_MAX = 16       # one LCD row
HELP_MAX = 90       # the help strip under a display
SHORT_VALUE_MAX = 5 # a value in a display column
KINDS = {"raw", "percent", "bipolar", "exp", "off_exp", "enum", "offset", "affine",
         "curve", "steps", "preset", "note", "fader", "pan"}


def load(engine_file):
    reg = json.loads(engine_file.read_text())
    help_file = REG / f"{reg['engine']}_help.json"
    if help_file.exists():
        extra = json.loads(help_file.read_text())
        groups = extra.get("_groups", {})   # readable names for the firmware's page categories
        for p in reg["params"]:
            if p.get("group") in groups:
                p["group"] = groups[p["group"]]
            e = extra.get(p["id"]) or extra.get(p.get("text", ""))
            if isinstance(e, str):
                e = {"help": e}
            if e:
                p.setdefault("help", e.get("help", ""))
                if not p["help"]:
                    p["help"] = e.get("help", "")
                if "name" in e:
                    p["name"] = e["name"]
    return reg


def check(reg):
    errors, warnings = [], []
    seen = set()
    for i, p in enumerate(reg["params"]):
        where = f"{reg['engine']} #{i} {p.get('id', '?')}"
        missing = [key for key in ("id", "short", "name", "min", "max", "default", "display") if key not in p]
        if missing:
            errors.append(f"{where}: no {', '.join(missing)}")
            continue
        if p["id"] in seen:
            errors.append(f"{where}: id used twice")
        seen.add(p["id"])
        if not 0 < len(p["short"]) <= SHORT_MAX:
            errors.append(f"{where}: short name {p['short']!r} is not 1 to {SHORT_MAX} characters")
        if len(p["name"]) > NAME_MAX:
            errors.append(f"{where}: name {p['name']!r} is longer than {NAME_MAX} characters")
        if not 0 <= p["min"] <= p["default"] <= p["max"] <= 255:
            errors.append(f"{where}: needs 0 <= min {p['min']} <= default {p['default']} <= max {p['max']} <= 255")
        d = p["display"]
        if d.get("kind") not in KINDS:
            errors.append(f"{where}: unknown display kind {d.get('kind')!r}")
        if d.get("kind") == "enum" and len(d.get("values", [])) != p["max"] - p["min"] + 1:
            errors.append(f"{where}: {len(d.get('values', []))} names for {p['max'] - p['min'] + 1} values")
        if d.get("kind") == "enum":
            shorts = d.get("short", d.get("values", []))
            if len(shorts) != len(d.get("values", [])) or any(len(x) > SHORT_VALUE_MAX for x in shorts):
                errors.append(f"{where}: needs a short name of at most {SHORT_VALUE_MAX} characters for every value"
                              " (display.short)")
        m = p.get("map")
        if m and "sst" in m and not m.get("sync") and d.get("kind") == "exp":
            # the map is linear in a log2 quantity: the display must say the same
            if d.get("unit") == "Hz" and m["hi"] - m["lo"] > 12:   # semitones from A4
                base, octaves = 440 * 2 ** (m["lo"] / 12), (m["hi"] - m["lo"]) / 12
            else:                                          # log2 seconds or log2 Hz
                base, octaves = 2 ** m["lo"], m["hi"] - m["lo"]
            if abs(d["base"] / base - 1) > 1e-4 or abs(d["octaves"] - octaves) > 1e-4:
                errors.append(f"{where}: display base {d['base']}, octaves {d['octaves']} do not match"
                              f" the map ({base:g}, {octaves:g})")
        h = p.get("help", "")
        if len(h) > HELP_MAX:
            errors.append(f"{where}: help is {len(h)} characters, more than {HELP_MAX}")
        if not h:
            warnings.append(where)
    return errors, warnings


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def gen_c(reg):
    e = reg["engine"]
    lines = [
        f"/* Generated by tools/registry.py from registry/{e}.json. Do not edit. */",
        f"#ifndef OM_REGISTRY_{e.upper()}_H_",
        f"#define OM_REGISTRY_{e.upper()}_H_",
        "",
        '#include "param_info.h"',
        f'#include "{reg["c_header"]}"',
        "",
        f"#define OM_{e.upper()}_NUM_PARAMS {len(reg['params'])}",
        "",
        "// every entry sits at its enum value",
    ]
    for i, p in enumerate(reg["params"]):
        lines.append(f'_Static_assert({p["id"]} == {i}, "registry/{e}.json: {p["id"]} is not entry {i}");')
    lines += ["", f"static const OmParamInfo om_{e}_params[OM_{e.upper()}_NUM_PARAMS] = {{"]
    for p in reg["params"]:
        flags = "OM_PARAM_UNUSED" if p.get("unused") else "0"
        lines.append(f'\t{{{c_string(p["id"])}, {c_string(p["short"])}, {c_string(p["name"])}, '
                     f'{p["min"]}, {p["max"]}, {p["default"]}, {flags}}},')
    lines.append("};")
    if any("map" in p for p in reg["params"]):
        # how each byte becomes the effect's own value (registry/fx.json "map")
        lines += ["", f"static const OmParamMap om_{e}_map[OM_{e.upper()}_NUM_PARAMS] = {{"]
        for p in reg["params"]:
            m = p["map"]
            kind = "OM_MAP_SYNC" if m.get("sync") else "OM_MAP_OWN" if m.get("own") else "OM_MAP_LINEAR"
            lines.append(f'\t{{{kind}, {m.get("sst", -1)}, {float(m.get("lo", 0))}f, {float(m.get("hi", 0))}f}},')
        lines.append("};")
    lines += ["", f"#endif /* OM_REGISTRY_{e.upper()}_H_ */", ""]
    return "\n".join(lines)


def gen_js(reg):
    keep = ("id", "short", "name", "group", "part", "min", "max", "default", "display", "help", "unused")
    params = [{k: p[k] for k in keep if k in p} for p in reg["params"]]
    body = {"engine": reg["engine"], "title": reg["title"], "parts": reg.get("parts", []), "params": params}
    return (f"// Generated by tools/registry.py from registry/{reg['engine']}.json. Do not edit.\n"
            f"export default {json.dumps(body, indent=1, ensure_ascii=False)};\n")


def main():
    only_check = "--check" in sys.argv
    files = sorted(f for f in REG.glob("*.json") if not f.stem.endswith("_help"))
    failed = False
    outputs = {}
    for f in files:
        reg = load(f)
        errors, warnings = check(reg)
        for err in errors:
            print(f"error: {err}")
        failed |= bool(errors)
        n = len(reg["params"])
        print(f"{reg['engine']}: {n} parameters, {n - len(warnings)} with help"
              + (f", {len(warnings)} still without" if warnings else ""))
        outputs[OUT_C / f"om_registry_{reg['engine']}.h"] = gen_c(reg)
        outputs[OUT_JS / f"{reg['engine']}.js"] = gen_js(reg)
    if failed:
        return 1
    stale = [p for p, text in outputs.items() if not p.exists() or p.read_text() != text]
    if only_check:
        for p in stale:
            print(f"out of date: {p.relative_to(ROOT)} (run tools/registry.py)")
        return 1 if stale else 0
    for p in stale:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(outputs[p])
        print(f"wrote {p.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
