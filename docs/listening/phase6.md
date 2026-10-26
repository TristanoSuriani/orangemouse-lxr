# Phase 6 listening checklist

The genre packs are tuned by ear (PLAN.md, "By ear"). Everything here was set from what the
instruments' controls do, measured for health and loudness, but not yet listened to.

Render the files, listen, and mark each line **keep** or **change** with a few words on what to change
(the notes become the next round of edits):

```sh
tools/render_songs.sh      # writes renders/<song>.wav and renders/plaits/<preset>.wav
```

The songs are also in the studio's library (SONGS on the tracker), so anything can be tried there.

## The songs

Measured on 2026-10-26: no clipping, nothing at the limiter, no DC; loudness (RMS) electro -15.3 dBFS,
synthwave -18.2, dub techno -17.0. Listened on 2026-10-26: all three kept. The trance song (two versions)
and "First steps" were taken out.

### Electro: Signal Lost (125 BPM, F minor, 38 s)

Beat, stabs and voice twice, lead twice.

| What | Meant to be | Keep / change |
|---|---|---|
| Kick (808-ish: lower, longer) | syncopated, boomy | |
| Clap, hats, cowbell-ish drum 3 | 2 and 4; busy 16ths with a roll; off-beat percussion | |
| Bass (Plaits FM bass, low-pass) | syncopated, rubbery | |
| Stabs (Dexed POWERCLAV) | off-beat Fm7 stabs, ping-pong delay | |
| Robot voice (Plaits speech) | a phrase every two bars | |
| Lead (VA3 Square Lead) | short eighths, right of centre | |
| The mix as a whole | | |

### Synthwave: Neon Coast (100 BPM, A minor, 58 s)

Intro, verse, chorus, verse, chorus, outro.

| What | Meant to be | Keep / change |
|---|---|---|
| Kick, snare | kick on 1 and 3; a fat snare with a long hall | |
| Bass (VA1 Moog Bass) | eighths pumping between root and octave | |
| Strings (Plaits Stringer) | the chords held, in the hall | |
| Rhodes (Dexed FmRhodes18) | off-beat chords | |
| Brass stabs (Plaits Poly brass) | a push at the end of each chorus bar | |
| Lead (VA3 Sync Lead) | the chorus melody, delay and hall | |
| The mix as a whole | | |

### Dub Techno: Grey Water (122 BPM, D minor, 47 s)

Groove, chords, sub, variation, sub, variation.

| What | Meant to be | Keep / change |
|---|---|---|
| Kick, hats, rim | deep and soft; dark off-beat hats; a rim now and then into the delay | |
| Chord stabs (Plaits Dub chord) | through a resonant low-pass into a long dotted delay and the hall: the dub chain | |
| Sub (Plaits Sub bass) | off-beat D, down to C every two bars | |
| Static (VA4 Static, high-pass) | a quiet hiss under everything | |
| Pad (Dexed OB8 PAD) | the variation's held Dm7 | |
| The mix as a whole | | |

## Plaits' presets

`renders/plaits/`: each preset plays a C4, a C3 and a C5, held 1 s then released. Volumes are balanced to
-21 dBFS (at most +6 dB, so Crackle, Deep pad, Stringer and Ping stay quieter).

| Preset | Genre | Engine | Keep / change | Notes |
|---|---|---|---|---|
| 01 Supersaw | trance | classic VA | | |
| 02 Pluck | trance | VA + filter | | |
| 03 Wave arp | trance | wavetable | | |
| 04 Trance pad | trance | string machine | | |
| 05 Hoover | trance | phase distortion | | |
| 06 FM bell | trance | 2-op FM | | |
| 07 Robot vox | electro | speech | | |
| 08 FM bass | electro | 2-op FM | | |
| 09 CZ bass | electro | phase distortion | | |
| 10 Fold lead | electro | waveshaper | | |
| 11 Zap | electro | VA + filter | | |
| 12 Vocoder | electro | chords | | |
| 13 Poly brass | synthwave | VA + filter | | |
| 14 Stringer | synthwave | string machine | | |
| 15 Retro bass | synthwave | classic VA | | |
| 16 Sync lead | synthwave | classic VA | | |
| 17 Glass keys | synthwave | 2-op FM | | |
| 18 Chord mem | synthwave | chords | | |
| 19 Dub chord | dub techno | chords | | |
| 20 Dub stab | dub techno | VA + filter | | |
| 21 Ping | dub techno | modal resonator | | |
| 22 Crackle | dub techno | particles | | |
| 23 Deep pad | dub techno | string machine | | |
| 24 Sub bass | dub techno | VA + filter | | |
