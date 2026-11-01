# "Later" listening checklist (2026-11-01)

The "Later" list added Surge's waveshapers, Rings and Clouds. Each one was tested for health and
measured, but nobody has listened to it yet. No song uses them yet. Try them in the studio, and mark
each line **keep** or **change**, with a few words on what to change.

## The waveshaper (the mixer, PAGE five times on a channel: SHP, DRV)

Sixteen of Surge's shapes, after the channel's filter. The output comes down by half the drive (+30 dB
in, about -15 dB out), and a soft knee above full scale holds the asymmetric shape under +6 dBFS. There
is no oversampling, so high notes at high drive may alias.

| What | Meant to be | Keep / change |
|---|---|---|
| soft, warm, tube on the VA bass at DRV 30-50 % | thicker and warmer, not louder | |
| hard clip and the fuzzes on a lead | aggressive, at about the same level | |
| fold, double fold, west coast on a sine-ish pad | harmonics that move with DRV | |
| octave and fifth | added harmonics, recognisably an octave and a fifth | |
| rectify, sine, digital | effects; usable at least at low DRV | |
| the level change as DRV turns | small: colour, not volume | |
| aliasing on high notes at full DRV | acceptable, or should it be oversampled? | |

## Rings (RN, track 16)

Eight presets, two per genre, balanced to Plaits' -21 dBFS. Notes ring until they fade (no note off,
as on the module), and a chord strums its notes 0.5 ms apart.

| Preset or feature | Meant to be | Keep / change |
|---|---|---|
| Glass bell (trance) | a bright, long modal bell for arps | |
| Harp (trance) | plucked strings, clean | |
| Metal hit (electro) | short metallic percussion | |
| FM pluck (electro) | a bright FM pluck | |
| Sitar (synthwave) | sympathetic strings that buzz along | |
| Chime pad (synthwave) | chords from a single key, ringing | |
| Dub ring (dub techno) | a string in its own reverb, the dub chord's cousin | |
| Wood (dub techno) | a dull wooden knock | |
| Velocity | harder notes brighter, not louder | |
| IN set to a drum (e.g. DR1, the kick muted) | the drum rings at the notes' pitch | |
| A4 on the string model is 440 Hz at STR 0, sharper as STR rises | right, or should STR not detune? | |

## Clouds (the mixer, FX A or B, TYPE + to "clouds")

All wet, 3 ms late. Its stretch mode needed a fix to Mutable's code (third_party/patches/eurorack.patch).

| What | Meant to be | Keep / change |
|---|---|---|
| granular on a pad send, the defaults | a cloud of grains behind the pad | |
| stretch | a smeared, slowed copy | |
| looping delay with PIT +12 | a sparkling loop an octave up | |
| spectral | a blurred, frozen-sounding wash | |
| FRZ on during a breakdown | the texture holds while the song moves on | |
