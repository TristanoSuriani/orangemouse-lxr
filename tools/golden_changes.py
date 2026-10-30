#!/usr/bin/env python3
# Copyright (C) 2026 Erica Synths
# SPDX-License-Identifier: GPL-3.0-only
#
# Which renders a golden file's update changes (PLAN.md Phase 7.5: re-baselining
# is deliberate). A golden line names a render, then its hash or health:
#   busy 0 63 8ca2937b30cadde8 peak=0.4045 ...     (lxr_params.txt)
#   electro hash 0c8e8af14f07d796                  (songs.txt)
#   health: 0 not finite, peak 0.105, ...          (stress.txt)
# The render's name is what comes before the hash, "hash" or "health"; a line
# with nothing before them belongs to the whole file.
#
#   tools/golden_changes.py <golden> <new>    prints "<n> of <m> renders: name, name, ..."

import re
import sys

NAME = re.compile(r'^(.*?)\s*(?:\b[0-9a-f]{16}\b|\bhash\b|\bhealth:)')


def renders(path):
    out = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.rstrip('\n')
                m = NAME.match(line)
                name = ' '.join((m.group(1) if m else line).split()) or '(the file)'
                out.setdefault(name, []).append(line)
    except FileNotFoundError:
        pass
    return out


def main():
    old, new = renders(sys.argv[1]), renders(sys.argv[2])
    names = list(dict.fromkeys([*old, *new]))
    changed = [n for n in names if old.get(n) != new.get(n)]
    added = [n for n in changed if n not in old]
    removed = [n for n in changed if n not in new]
    shown = changed[:40]
    text = f"{len(changed)} of {len(names)} renders"
    if added and len(added) == len(changed):
        text += " (new)"
    elif removed:
        text += f" ({len(removed)} removed)"
    print(text + (": " + ", ".join(shown) + (", ..." if len(changed) > len(shown) else "") if changed else ""))


if __name__ == '__main__':
    main()
