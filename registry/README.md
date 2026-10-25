# The parameter registry

Every engine's parameters are described once, here (PLAN.md Phase 0, step 7). Everything else is generated
from these files or checked against them.

| File | What it is |
|---|---|
| `va.json` | Orange Mouse VA, written by hand |
| `lxr.json` | The LXR, **generated** from the firmware by `tools/lxr_registry.py`: don't edit it |
| `lxr_help.json` (to come, Phase 1.3) | The LXR's help lines and better names, by text id (`TEXT_*`) or parameter id |

A parameter has:
- `id`: the engine's enum name, e.g. `VA_P_CUTOFF`.
- `short`: the LCD's 3 characters.
- `name`: up to 16 characters, one LCD row.
- `group`: its page.
- `part`: the voice it belongs to, if any.
- `min`, `max`, `default`.
- `display`: how to show the value.
- `help`: one line, up to 90 characters.
- `unused` (optional): the engine ignores it.

Display kinds (`web/app/registry/format.js`):

| Kind | Shows | Example |
|---|---|---|
| `raw` | the value | `64` |
| `percent` | the share of the range | `50 %` |
| `bipolar` | `(v - center) × scale`, signed | `+7 st` |
| `exp` | `base × 2^(v × octaves / 127)`, with a readable unit | `1.2 kHz`, `14 ms` |
| `off_exp` | as `exp`; 0 is `off` | `off`, `0.3 s` |
| `enum` | a name per value | `saw` |
| `offset` | `v + offset` | `+2 oct`, `7 voices` |
| `affine` | `a + b × v` | `48 %` |
| `curve` | `scale × (v / 127)^power` | `3.00 st` |
| `steps` | `(v + 1) / 16` sequencer steps | `1/4 step` |
| `preset` | a preset number | `#3` |
| `note` | a MIDI note name | `C3` |

## Generated

`tools/registry.py` checks every file and writes:
- `generated/registry/om_registry_<engine>.h`: the `OmParamInfo` table each engine exposes through
  `engines/engine.h`, plus a compile-time check that every entry sits at its enum value;
- `web/app/registry/<engine>.js`: the UI's labels, value display and help.

It refuses:
- short names longer than 3 characters;
- names longer than 16;
- help lines longer than 90;
- defaults outside the range;
- value lists that don't match the range.

## Checked by `tests/run.sh`

- `tools/lxr_registry.py --check`: `lxr.json` still matches the firmware.
- `tools/registry.py --check`: every file is valid and the generated files are up to date.
- `tests/registry_test.mjs`: every value of every parameter formats to readable text of at most 12
  characters, and a few values match VA's formulas.
- The C compile-time checks.
- `lxr_sweep` takes its test values from these ranges.

After editing a registry file, or changing the firmware's parameters, run `tools/lxr_registry.py` (after
building `tests/host`) and `tools/registry.py`, then commit the generated files with the change.
