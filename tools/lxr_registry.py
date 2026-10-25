#!/usr/bin/env python3
# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
"""Writes registry/lxr.json, the LXR's parameter registry, from the firmware itself.

The firmware already describes its parameters; this reads those tables so the
registry cannot drift from them. It reads them from Core/Src/menu/menu.c run
through the C preprocessor with the host build's flags (tests/host/build/
compile_commands.json), so every #if in the enums resolves as in the build:

  Core/Inc/Parameters.h     the parameter enum (PAR_*)
  Core/Inc/menu/menuPages.h each menu page: 8 text ids (TEXT_*) over 8 parameters
  Core/Src/menu/menu.c      valueNames: a text id's short, category and long name
                            parameter_dtypes: each parameter's type (range, display)
                            getMaxEntriesForMenu: which name list a menu type uses
  Core/Inc/menu/MenuText.h  the names themselves, and the menus' value names
  tests/host lxr_render --dump-kit   the init kit's values, as the defaults

Help lines are not in the firmware: they come from registry/lxr_help.json, by
text id (TEXT_*), so one line helps every voice's parameter of that name.

    tools/lxr_registry.py            (after building tests/host)
    tools/lxr_registry.py --check    fail if registry/lxr.json no longer matches the firmware
"""
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "tests" / "host" / "build"


def preprocessed_menu_c():
    """menu.c (with Parameters.h, menu.h, menuPages.h and MenuText.h) after the preprocessor."""
    commands = json.loads((BUILD / "compile_commands.json").read_text())
    entry = next(c for c in commands if c["file"].endswith("Core/Src/menu/menu.c"))
    args = shlex.split(entry["command"])
    out = args.index("-o")
    args = args[:out] + args[out + 2:]
    args = [a for a in args if a != "-c"] + ["-E", "-P", "-w"]
    return subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True,
                          check=True, encoding="latin-1").stdout


def parse_enum(text, first):
    """name -> value for the enum containing the identifier `first`."""
    start = text.rfind("{", 0, re.search(r"\b" + first + r"\b", text).start())
    body = text[start + 1:text.index("}", start)]
    values, value = {}, -1
    for item in (i.strip() for i in body.split(",")):
        if not item:
            continue
        m = re.match(r"(\w+)\s*(?:=\s*(.+))?$", item, re.S)
        if not m:
            continue
        name, expr = m.group(1), m.group(2)
        if expr is not None:
            expr = expr.strip()
            value = int(expr, 0) if re.match(r"^(0x)?[0-9a-fA-F]+$", expr) else values[expr]
        else:
            value += 1
        values[name] = value
    return values


def array_body(text, name):
    m = re.search(r"\b" + name + r"\s*\[[^\]]*\](?:\s*\[[^\]]*\])?\s*=\s*\{", text)
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def string_table(text, name):
    """const char name[][N] = { {"a"}, {"b"} } -> ["a", "b"]; a leading {count} is kept as an int."""
    out = []
    for m in re.finditer(r'\{\s*(?:"((?:[^"\\]|\\.)*)"|(\d+))\s*\}', array_body(text, name)):
        out.append(m.group(1) if m.group(1) is not None else int(m.group(2)))
    return out


def main():
    # one preprocessed text holds every table; the names below say what each lookup is for
    params_h = menu_h = pages_h = menu_c = text_h = preprocessed_menu_c()

    par = parse_enum(params_h, "PAR_NONE")
    n = par["END_OF_SOUND_PARAMETERS"]
    names_by_index = {}
    for name, value in par.items():
        if value < n and name.startswith("PAR_") and not name.startswith("PAR_BEGINNING"):
            names_by_index[value] = name   # an alias defined later (PAR_OSC_WAVE_DRUM1 = PAR_MOD_WHEEL) is the current name

    text_ids = parse_enum(menu_h, "TEXT_EMPTY")
    short_enum = parse_enum(menu_h, "SHORT_EMPTY")
    cat_enum = parse_enum(menu_h, "CAT_EMPTY")
    long_enum = parse_enum(menu_h, "LONG_EMPTY")
    short_names = string_table(text_h, "shortNames")
    cat_names = string_table(text_h, "catNames")
    long_names = string_table(text_h, "longNames")

    # valueNames: one {SHORT_, CAT_, LONG_} per text id, in TEXT_ order
    value_names = re.findall(r"\{\s*(SHORT_\w+)\s*,\s*(CAT_\w+)\s*,\s*(LONG_\w+)\s*\}", array_body(menu_c, "valueNames"))
    text_name = {v: k for k, v in text_ids.items()}
    names_of_text = {}
    for i, (s, c, l) in enumerate(value_names):
        if i in text_name:
            names_of_text[text_name[i]] = (short_names[short_enum[s]], cat_names[cat_enum[c]], long_names[long_enum[l]])

    # menuPages: 8 text ids, then the 8 parameters below them
    tokens = re.findall(r"\b(TEXT_\w+|PAR_\w+)\b", array_body(pages_h, "menuPages"))
    text_of_param = {}
    i = 0
    while i + 16 <= len(tokens):
        texts, pars = tokens[i:i + 8], tokens[i + 8:i + 16]
        if all(t.startswith("TEXT_") for t in texts) and all(p.startswith("PAR_") for p in pars):
            for t, p in zip(texts, pars):
                if p in par and t != "TEXT_EMPTY":
                    text_of_param.setdefault(par[p], t)
            i += 16
        else:
            i += 1

    # types: parameter_dtypes, in parameter order
    dtypes = [d.strip() for d in array_body(menu_c, "parameter_dtypes").split(",") if d.strip()]
    # getMaxEntriesForMenu: after the preprocessor, "case 18: return fxNames[0][0];" (the
    # waveforms add the SD card's samples: none in the studio)
    menu_array = {int(k): v for k, v in re.findall(r"case\s+(\d+)\s*:\s*return[^;]*?(\w+)\s*\[0\]\[0\]", menu_c)}

    # the PM63 parameters the display shows as value - 60, not - 63 (the coarse
    # tunings and modulator frequencies: menu.c's "curParmVal - 60" branch)
    m = re.search(r"if\s*\(([^{}]*?)\)\s*\{\s*numtostrps\s*\([^,]+,\s*\(\s*int8_t\s*\)\s*\(\s*curParmVal\s*-\s*60", menu_c)
    names = re.findall(r"parNr\s*==\s*(\w+)", m.group(1)) if m else []
    center60 = {int(x) if x.isdigit() else par[x] for x in names}
    if not center60:
        raise SystemExit("menu.c: the display's 'curParmVal - 60' list was not found")

    defaults = {}
    dump = BUILD / "lxr_render"
    out = subprocess.run([str(dump), "--dump-kit"], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        k, v = line.split()
        defaults[int(k)] = int(v)

    voices = ["Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hihat"]

    def part_of(pid):
        # named voices first (PAR_WAVE1_CYM is the cymbal's), then a voice number
        for pat, label in (("DRUM1", 0), ("DRUM2", 1), ("DRUM3", 2), ("SNARE", 3), ("CYM", 4), ("_HH|HH$", 5)):
            if re.search(pat, pid):
                return voices[label]
        m = re.search(r"(?:TRANS|_)(\d)(?:_|$)|(\d)$", pid)
        d = m and int(m.group(1) or m.group(2))
        return voices[d - 1] if d and 1 <= d <= 6 else ""

    params = []
    for idx in range(n):
        pid = names_by_index.get(idx, f"PAR_INDEX_{idx}")
        dt = dtypes[idx]
        kind = re.match(r"(DTYPE_\w+)", dt).group(1)
        lo, hi, display = 0, 127, {"kind": "raw"}
        if kind == "DTYPE_0B255":
            hi = 255
        elif kind == "DTYPE_PM63":
            display = {"kind": "bipolar", "center": 60 if idx in center60 else 63, "scale": 1, "unit": ""}
        elif kind in ("DTYPE_ON_OFF", "DTYPE_0b1"):
            hi = 1
            display = {"kind": "enum", "values": ["off", "on"]} if kind == "DTYPE_ON_OFF" else {"kind": "offset", "offset": 1, "unit": "", "signed": False}
        elif kind == "DTYPE_1B16":
            lo, hi = 1, 16
        elif kind == "DTYPE_1B64":
            lo, hi = 1, 64
        elif kind == "DTYPE_0B15":
            hi = 15
        elif kind == "DTYPE_0B100":
            hi = 100
        elif kind == "DTYPE_VOICE_LFO":
            lo, hi = 1, 6
        elif kind == "DTYPE_NOTE_NAME":
            display = {"kind": "note"}
        elif kind == "DTYPE_MENU":
            menu = int(re.search(r"\(\s*(\d+)\s*<<", dt).group(1))
            values = string_table(text_h, menu_array[menu])
            count, values = values[0], [v.strip() for v in values[1:]]
            hi = count - 1
            display = {"kind": "enum", "values": values[:count]}
        default = min(max(defaults.get(idx, lo), lo), hi)
        text = text_of_param.get(idx, "")
        short, cat, long_ = names_of_text.get(text, ("", "", ""))
        entry = {
            "id": pid,
            "short": short.strip() or pid.replace("PAR_", "")[:3].lower(),
            "name": (long_.strip() or pid.replace("PAR_", "").replace("_", " ").title())[:16],
            "group": cat.strip(),
            "part": part_of(pid),
            "min": lo, "max": hi, "default": default,
            "display": display,
            "text": text,
        }
        if not text:
            entry["unused"] = True   # on no menu page: not reachable from the panel
        params.append(entry)

    reg = {"engine": "lxr", "title": "LXR", "c_header": "Parameters.h",
           "parts": ["Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hihat closed", "Hihat open"],
           "params": params}
    path = ROOT / "registry" / "lxr.json"
    text = json.dumps(reg, indent=1, ensure_ascii=False) + "\n"
    if "--check" in sys.argv:
        if not path.exists() or path.read_text() != text:
            print("registry/lxr.json no longer matches the firmware: run tools/lxr_registry.py")
            return 1
        return 0
    path.write_text(text)
    on_pages = sum(1 for p in params if p["text"])
    print(f"registry/lxr.json: {n} parameters, {on_pages} on a menu page")
    return 0


if __name__ == "__main__":
    sys.exit(main())
