# Phase 7 listening checklist

Phase 7 (safety) was meant to change no sound. Two fixes changed some on purpose, and
tests/golden/CHANGES.md lists every render they touched. Render, listen, and mark each line **keep**
or **change**:

```sh
tools/render_songs.sh      # writes renders/<song>.wav and renders/plaits/<preset>.wav
```

## Plaits at its own rate (Phase 7.6)

Plaits is built for its hardware's 47,872 Hz. At the studio's 44.1 kHz its notes were tuned with a
+1.42-semitone correction, but its envelopes, its low-pass gate and its engines' own decays ran 6 to 9 %
slow (measured: a 40 dB fall took 0.765 s instead of 0.719 s on Ping, 1.218 s instead of 1.119 s on
Glass keys). Now Plaits runs at 47,872 Hz and is resampled to 44.1 kHz, so its time, its filters and its
tuning are those of the module. Its decays now match within 1.2 %. Loudness moved by at most 0.2 dB.

| What | Expect | Keep / change |
|---|---|---|
| Electro: the FM bass (PL1) and the robot voice (PL2) | the same parts, slightly tighter | |
| Synthwave: its Plaits parts | the same, slightly tighter | |
| Dub techno: the chord stab (PL1) and its other Plaits part | the same, slightly shorter tails | |
| renders/plaits/: the percussive presets (Pluck, Ping, FM bell, Dub stab, Glass keys) | as on the module: about 8 % shorter than before | |

## The LXR's oscillators wrap correctly (Phase 7.3)

The fuzz found the LXR's wavetable, FM and PWM oscillators reading one sample past the end of their
waves, once per cycle: the next wave's first sample, or memory past the last wave. They now wrap to
the wave's own start. Of the regression renders, only the "busy" kit's sweep changed, with peak and DC
the same to 4 decimals. The demo groove and the songs did not change.

| What | Expect | Keep / change |
|---|---|---|
| The LXR in any song: anything you knew | no difference you can hear | |
