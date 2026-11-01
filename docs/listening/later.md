# "Later" listening checklist (2026-11-01)

The "Later" list added Surge's waveshapers, Rings and Clouds. Each one was tested for health and
measured, but nobody has listened to it yet. The four genre songs use them (electro, synthwave and dub
techno at version 2, trance at version 2 of "Dawn Run 3"; the library keeps your old copies). Try them in the studio, and mark each line **keep** or **change**, with a
few words on what to change.

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

## In the genre songs (version 2)

Set by measurement only: each song kept its loudness (electro -15.3 dBFS, dub techno -16.8, synthwave
-17.7, from -18.2; Rings adds to it; trance -15.4, from -15.5). The drives are low where the shaper fattened the kick's tail.

| Song | What was added | Keep / change |
|---|---|---|
| Electro | the FM bass through *warm* (DRV 40), the kick through *tube* (DRV 20) | |
| Electro | Rings' *Metal hit* answering the robot voice, four notes every two bars | |
| Electro | Clouds on FX A instead of the room: the voice and the hits as grains, an octave up (the snare lost its room send) | |
| Synthwave | the VA bass through *tube* (DRV 40), the snare through *soft* (DRV 35) | |
| Synthwave | Rings' *Chime pad* chiming the chord notes in the intro and outro | |
| Dub techno | the chord stab through *warm* (DRV 45), the kick through *soft* (DRV 15) | |
| Dub techno | Rings' *Dub ring* struck by the rim (IN DR3), tuned D3/A2, F3/C3 in the variation | |
| Dub techno | Clouds on FX A instead of the hall: a big smooth grain wash with its own reverb | |
| Trance | the bass through *warm* (DRV 30), the kick through *tube* (DRV 15) | |
| Trance | Rings' *Harp* strumming the chords in the breakdown | |
| Trance | Clouds on FX B instead of the tempo delay: a shimmer an octave up behind the pluck, the anthem and the harp | |
