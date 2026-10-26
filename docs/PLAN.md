# Orange Mouse: build plan

A browser studio made of hardware-looking devices: a tracker, the LXR, Orange Mouse VA, Plaits and Dexed,
mixed through Surge XT effects. It is built for one user, so every choice favours less rework and fast
feedback over generality. Architecture: `docs/architecture-v2.svg`.
Live status of every step: [Orange Mouse Build](https://claude.ai/artifact/QuzWvua6PxLsX7kEV89q6h).

## Principles

1. **One source of truth per fact.** A parameter (id, short LCD name, full name, range, unit, default,
   one-line help) is declared once and generated into C and JS. Presets, the UI, the help text, the MIDI
   CC map and the tests all read it, so nothing is typed twice.
2. **Audio is C, the UI is a view.** The sequencer, router, engines and mixer run as C in the worklet. The
   same C builds natively on the Mac, so almost every test runs without a browser.
3. **Panels are data.** A device panel is a declaration (controls, positions, parameter ids). One component
   kit draws every panel, which makes the look consistent and the layout checkable by a script.
4. **Retire risks first.** Compiling foreign code (Plaits, Dexed, Surge) to WebAssembly is the riskiest
   part, so it is spiked in week one, before anything depends on it.
5. **Build each thing once.** The UI kit is finished on one device before the tracker is drawn with it.

## The devices

| Device | Role | Sounds |
|---|---|---|
| Tracker | Sequencer and, when used, the master clock; 16 tracks on a huge dot-matrix display | Pure synthesis: it only sends notes |
| LXR | 7 drum voices, the existing firmware | Its kits; its own sequencer runs alone or follows the tracker |
| Orange Mouse VA | 4 slots: VA1 mono bass, VA2 poly, VA3 mono lead, VA4 noises | Preset sets per slot (built last session) |
| Plaits | Macro oscillator, limited to the engines the genres need | Genre presets |
| Dexed | DX7 FM, **presets only** (pick a voice; volume and transpose) | Curated .syx banks |
| Mixer | Audio Composer: level, pan, sends, master bus | Surge XT effects (`sst-effects`) |

**First genres:** trance, electro, synthwave, dub techno. IDM comes later.

Plaits engines for these four genres (Plaits 1.2 numbering to be confirmed in the spike):

| Genre | Plaits engines |
|---|---|
| Trance | Swarm (supersaw), virtual analog, wavetable, string machine |
| Electro | FM, speech, analog bass drum / snare / hi-hat, waveshaping |
| Synthwave | Virtual analog, string machine, chords |
| Dub techno | Chords (the stab), modal, filtered noise |

## UI: one hardware language, in the LXR skin

**The look is the current LXR web skin** (`web/app/style.css`), applied to every device. Its tokens become
the shared design tokens, and nothing gets a style of its own:

| Element | LXR skin |
|---|---|
| Room and panel | `--page #17181b`; graphite panel `--panel #26282d` with a top gradient and a `--panel-edge` rim |
| Display | Amber dot matrix `--lcd-on #ffc94d` on black `--lcd-bg`, unlit dots `--lcd-off`, 5×7 font (`lcd.js`) |
| LEDs | One colour: orange `--led-on #ff8a1f` with a glow; off is `--led-off` |
| Pads | Raised `--pad` buttons with a main label, a small grey second-function label (`.alt`) and an LED |
| Pots and encoder | Dark knobs with a pointer mark; endless pots |
| Faders | The LXR's vertical faders |
| Focus | The blue `--focus` ring, the one non-hardware colour, kept for keyboard users |

- **Displays.** Small devices get character modules like the LXR's 16×2. The tracker gets a large graphic
  module with the same dot pitch and amber, so it reads as the same family.
- **One LED colour.** Meaning comes from position, label and blink, as on the LXR: lit is on, blinking is
  armed or cued (waiting for the downbeat). There are no green or red LEDs.

**One interaction grammar, the same on every device:**

- **Soft knobs.** Four knobs sit under each display, and the display labels them on its bottom row (as on
  the LXR and TRIS Handheld). Knowing this on one device means knowing it on all of them.
- **Gestures.** Drag or scroll a knob. Shift makes it fine. Double-click resets it to its default.
  Alt-click latches a pad.
- **Second functions** are printed in small grey under the pad's label (the LXR's `.alt`) and reached
  with SHIFT, on every device.
- **Page buttons** sit in a row under the display, numbered in the same order on every device.

**Understandable without a manual.** These are the accessibility features that matter most here:

- **The help line.** Touching, hovering or focusing any control shows its full name, value with unit, and
  one-line help on a strip under that device's display. Example: `SEM: Osc 2 tuning, in semitones. +7 = a
  fifth up`. The text comes from the parameter registry.
- **An LXR glossary.** LXR's abbreviations (`NTY`, `SLP`, `ENV`...) are expanded on the LXR's help line.
  The glossary is built from `Core/Inc/menu/MenuText.h` plus hand-written descriptions, and the firmware
  doesn't change.
- **A `?` overlay on every device**, like a printed overlay card on hardware. It labels every control and
  says what each page is for.
- **Keyboard and screen readers.** Every control can be reached by Tab, has a role, a name and a spoken
  value (`aria-valuetext` = the help line's value), and shows a visible focus ring.
- **Accessible colour and motion.** LED colours meet contrast against the panel, and animation respects
  reduced motion.

**Rules from TRIS Tracker that every device follows:**

- **A printed legend.** The five most-used keys for a device are printed on its panel, like a hardware
  legend strip. The `?` overlay explains *how to work*, grouped by task, not just a list of keys.
- **The keybed shows its keys.** Each piano key carries its computer-key letter and note name, and lights
  when played from anywhere.
- **Undo for everything**, knob drags included: the value before the drag is saved on press and committed
  on release. Undo never moves the view.
- **It saves itself.** Every edit autosaves; there is no Save button to forget.
- **Auditioning sounds neutral.** Previewing a sound plays it through a clean path, not through the
  selected track's effects.
- **You see what sounds.** A step, pad or LED flashes when its note actually plays, not when it's
  scheduled.
- **Long jobs say so first.** The display shows `RENDERING…` before a long job starts.
- **Nothing is lost by accident.** Removing a track keeps its notes for when it comes back; shortening a
  pattern keeps what fits; old file formats always load.

**Layout: the whole studio fits on one page.** Desktop screens are the target (1280×800 and up); phones are
not a priority.

- **One device expanded at a time** (decided 2026-10-25, after two expanded panels overflowed a 16"
  MacBook Pro). The others are cards in the top bar: name, a small dot-matrix display mirroring the device,
  and an LED on the expanded one. The expanded device takes the computer keyboard.
- **No scrolling.** The browser test checks, for every device, that the page fits a 1280×800 window.
- **Moving between devices.** Click a card, or use Alt+1…Alt+9 (in the cards' order) and Alt+←/→. ⌘1 and
  Ctrl+Tab belong to the browser and cannot be taken reliably.
- **Playing only the LXR.** Turning the tracker off hands the clock to the LXR's own sequencer. The LXR then
  plays alone, exactly as the hardware does, and the other devices still answer MIDI.

## The tracker

TRIS Tracker's usability rules and features, redrawn as a hardware device in the LXR skin. Its own look
isn't used. Where the rule came from Fel-Tracker or TRIS Handheld instead, it says so.

**Pure synthesis.** A track plays an instrument on a device (LXR voice, VA slot, Plaits slot, Dexed
slot). There are no samples.

**Writing notes**

- **Edit and jam are separate modes** (ESC). With EDIT off, the keys only play. The cursor is drawn
  dimmer, so you can see notes are safe.
- **STEP sets how far the cursor drops** after a note: 1 fills every row, 4 writes on the beat, and 0
  keeps the cursor still (for auditioning, or rewriting one row). Row numbers where the cursor will land
  are tinted. Backspace steps back by the same amount.
- **Chords by stacking.** SHIFT + a note adds it to the current cell, up to 4 voices. The cell shows ²³⁴,
  and stacked voices are level-matched so a chord isn't louder. Without a SHIFT key, a CHORD pad latches
  stacking on.
- **Keys.** The Z–M and Q–I rows are two octaves, A is note off, and `[` `]` change the octave. Each track
  remembers its own octave.

**Selecting and moving**

- **No selection means one cell.** Copy and paste work on the cell under the cursor: copy a note, move,
  paste.
- **Blocks.** SHIFT + arrows (or a drag) selects a rectangle across tracks. After a copy, the cursor
  jumps to the block's top-left, so moving down and pasting repeats it. Paste lands top-left at the
  cursor, clipped to the pattern. Paste-in-place (⌘⇧V) always returns it to the same tracks.
- **Select all, twice.** The first ⌘A takes the track, the second takes every track.
- **Mute and solo from the track header:** a click mutes (the name is crossed out); a right-click solos
  (the others dim).

**Patterns and the song**

- **Patterns own their tempo and effects**, so an intro can sit behind a filter and the drop can open up.
  The change lands on the downbeat. This replaces Fel-Tracker's variations, like its PAT SET.
- **Editing and playing are separate.** The pattern you edit doesn't have to be the one playing. LOOP
  holds the edited pattern. Picking a pattern while playing jumps there.
- **Songs end.** The order list doesn't wrap. PLAY after the end starts from the top.
- **Three views of the song:** the order, the pattern pool, and the playlist.

**Library** (what TRIS's library covered)

- **Songs** save themselves as you type: new, duplicate, rename inline, export and import JSON, export WAV
  (an offline render of the same audio core), a share code, and demo songs.
- **A playlist** queues songs that hand over one to the next with a set gap. You can reorder it and start
  anywhere.
- **Sounds** come from each device's preset library. This adds Fel-Tracker's genre tags.

**Perform view.** The big display switches to full-screen visuals that react to each track's level.

**The cell** is the TRIS cell plus RETRIG: up to 4 stacked notes, a velocity, and a RETRIG (2 to 8 hits in
the row, as in Fel-Tracker).

**16 tracks**, 8 on the display at a time, with the header showing which half is on screen.

## Phases

Each phase ends with the tests green and a short listening checklist for you. The order is chosen so that
nothing built earlier has to be redone.

### Phase 0: foundations (sets everything else)

1. **Separate VA from LXR.** Return the LXR firmware to 7 tracks and keep `VaEngine.c` and `VaPresets.c` as
   a standalone engine. The 11-track edits in the sequencer, menus, button/LED maps and kit file were made
   last session and are not committed yet, so this is a selective revert.
2. **Spikes.** Compile a Plaits engine, the Dexed MSFA core and one `sst-effects` effect to WebAssembly,
   then measure CPU per voice and per effect. The biggest unknown is Surge's SIMD under WebAssembly.
   A spike that fails changes this plan here, not later.
   **Done 2026-10-25: all three pass** (`spikes/README.md`). Each costs at most a few percent of the
   audio thread. Native and WebAssembly output match bit for bit (Plaits) or within 1 LSB.
3. **Native host build.** The whole audio core builds as a Mac command-line tool with the web port's stubs.
4. **Golden baseline.** Render the LXR presets *before* touching the sample rate.
5. **The engine interface in C:** `init`, `note_on`, `note_off`, `set_param`, `render(block)` returning
   stereo stems. LXR and VA are wrapped first. Then the router and the mixer skeleton.
6. **The studio's sample rate.** ~~Move to 48 kHz~~. **Decided 2026-10-25: the studio runs at 44.1 kHz.**
   - Much of the LXR's DSP works in fractions of the sample rate: the filter cutoff is a fraction of
     Nyquist, the transients advance one sample per sample, and the delay buffer is a fixed number of
     samples. At 48 kHz its filters would sit 1.5 semitones higher and its transients and envelopes would
     run 9 % faster, unless the whole firmware were audited.
   - VA, Dexed and the Surge effects take the rate as a parameter.
   - Plaits is the only engine designed for about 48 kHz (47.9 kHz). Its adapter corrects its pitch
     (+1.42 semitones, measured in Phase 5), or resamples that one voice.
   - The browser converts 44.1 kHz to the sound card's rate.
   - `REAL_FS` (44140 on the hardware, whose clock cannot make 44100) is 44100 in our builds, so the LXR
     is in tune with the other engines. That moved its pitch by 1.6 cents and was re-baselined on purpose.
7. **The parameter registry format** and its generator for C and JS. **Done 2026-10-25** (`registry/README.md`):
   - VA's 49 parameters are written by hand, each with a help line.
   - The LXR's 247 are extracted from the firmware's own tables and checked against it. Their help lines
     are Phase 1.3's glossary.

### Phase 1: the UI kit, on Orange Mouse VA

Build the component kit (display renderer, knob, button with LED, fader, panel frame, help line, `?`
overlay, keyboard handling) on the VA panel: it's new and fully under our control. Lock the interaction
grammar here. Then re-skin the existing LXR panel with the kit and add its glossary.

### Phase 2: the tracker

Write the sequencer core in C (sample-accurate, testable natively) and draw the huge display with the kit.
It covers patterns, the song chain, track-to-device assignment and the library. The clock is switchable:
the tracker leads, or the LXR plays alone on its own sequencer. It plays LXR and VA.

### Phase 3: the mixer and Surge effects

Per-track stems, sends and the master bus, using the effects that passed the Phase 0 spike (Reverb2,
Reverb1, Delay, Phaser, Flanger). sst-effects has no chorus yet, so dub techno's chorus is ported from
Surge XT or written here.

Done as follows:

- **Channels are stems.** Every engine stem is a channel: the LXR's six voices and its own FX bus
  (web/port/web_lxr_stems.c taps each voice before the firmware's output routing), and VA's four slots.
  Each has level, pan, two sends, mute and solo. Gains glide over one block when they change.
- **Two send buses**, each with any of six effects: Surge's Reverb2 ("hall"), Reverb1 ("room"), Delay
  (its times are tempo divisions that follow the tracker), Phaser and Flanger, and a chorus written here
  (studio/fx.cpp). Bus A starts as a hall, B as a delay. registry/fx.json describes the effects'
  settings, with musical defaults and ranges; its `map` says how each byte becomes the Surge value, and
  tools/registry.py checks that each display matches its map.
- **The master** has a level and a lookahead limiter (-0.2 dBFS, 64 frames = OM_STUDIO_LATENCY).
- **The mix is part of the song** (song.mix), so it saves and loads with it.
- **Parity holds.** Surge's effects run on our own configuration with a seeded random generator, and the
  native and browser renders of the demo song, effects included, are still bit for bit the same.
- **Not yet:** an EQ.

Added on 2026-10-26: **Surge filters on every channel.** Each mixer channel gets an insert filter from
Surge XT (sst-filters, already fetched with the effects): its filter types (low-pass, high-pass,
band-pass, notch, the ladders and the vintage models) with cutoff and resonance on the channel's
strip, for the sweeps that trance and dub techno live on.

### Phase 4: Dexed

Presets only: a bank browser on a character display, and volume and transpose on soft knobs.

Done as follows: Dexed's engine (msfa and EngineMkI, with the three shims of the spike, now in
engines/dexed/shim/) plays the 33 cartridges Dexed ships with: 1,056 DX7 sounds, compiled in from
third_party at build time (tools/dexed_carts.py). Two parts, DX1 and DX2, each with 16 voices, on tracker
tracks 12 and 13 and on their own mixer strips; Dexed's own voice stealing, LFO and output clipping. The
device lists a bank's 32 sounds on its display, four columns of names; each part's bank, sound, volume
and transpose are part of the song. tests/host/dexed_test.c checks every sound, and the native and
browser renders stay bit for bit the same.

### Phase 5: Plaits

The genre-picked engines, with presets for each genre.

Done as follows: eleven of Plaits' engines, picked for the genres (VA with filter, classic VA,
waveshaper, 2-op FM, phase distortion, wavetable, chords, string machine, speech, modal resonator,
particles; the drums stay with the LXR). engines/plaits/om_plaits_voice.cc is a copy of Plaits' voice
with only those engines, float output (no 16-bit clip) and no trigger delay. Two parts, PL1 and PL2, of
four voices each, on tracker tracks 14 and 15; the gate drives Plaits' trigger and, through an attack
envelope and the velocity, its low-pass gate. Every note is corrected by +1.42 semitones for 44.1 kHz:
A4 measures 440.0 Hz. Six presets per genre (engines/plaits/plaits_presets.c), starting points for the
genre packs to tune by ear. The device lists every preset in a column per genre and names the three
macro knobs in each engine's words.

### Phase 6: genre packs

Presets and one demo song per genre for trance, electro, synthwave and dub techno. These demo songs also
become the regression songs. IDM comes later.

Built on 2026-10-26, waiting to be tuned by ear (docs/listening/phase6.md):

- **Songs carry every sound.** A song keeps VA's slots and the LXR's kit as well as the mix, Dexed and
  Plaits, so a genre song brings its own drums and synths.
- **Songs**, written in code (web/app/tracker/genres/): electro (125 BPM), synthwave (100) and dub
  techno (122), 38 to 58 s each (a trance song too, at first; see below). Each uses the LXR, VA, Dexed and Plaits, the sends and the
  channel filters. They are in the library on first start.
- **They are the regression songs.** tools/song_stream.mjs turns a song into the message stream the page
  sends; tests/host/song_render.c replays it through the whole studio. tests/run.sh renders each song
  whole (hash, health, loudness, and a failure if an instrument the song plays stays silent); one song
  also has a CPU budget and is checked bit for bit against WebAssembly.
- **Plaits' presets** moved into registry/plaits.json (one source for C and the page), their volumes
  balanced to -21 dBFS by measurement.
- **The master has a DC blocker** (5 Hz) before its limiter: the first renders carried up to 0.0045 of DC.
- **To listen:** tools/render_songs.sh writes the songs and Plaits' presets as WAV files into renders/.

Listened on 2026-10-26: electro, synthwave and dub techno are kept. The trance song (in two versions) and
"First steps" were taken out, with no replacement: a fresh studio opens the library's newest song, and the
studio deletes those two from an existing library once. The genre packs are three songs; the electro song
now carries the CPU budget and the WebAssembly check.

### Phases 7 to 10: added on 2026-10-26, after a review of the product

A review after Phase 6 found that what limits the songs now is the mixing tools and the tracker, not
the engines, and it named the risks below. The new phases come before "Later", in this order: the
risks first, then what the plan promised and doesn't have yet, then the mix, then the tracker. Trance
comes back last, once the tools it needs exist.

### Phase 7: safety (the risks)

1. **Songs can't be lost.** The library lives only in IndexedDB and localStorage, which a browser may
   evict (Safari does after 7 days without a visit). Ask for `navigator.storage.persist()` at start and
   say on the tracker's display if it was refused. Add a backup: a folder picked once (File System
   Access) that receives every song as .json after each autosave, or, where that isn't available, an
   `EXPORT?` reminder when the songs haven't been exported for a week.
2. **CPU in the browser, at the worst case.** Write a stress song with every part at its voice limit
   (Dexed's 32 voices, both Plaits parts, the four VA slots, the LXR), both sends on reverbs and every
   channel filter on. Time it in WebAssembly, in a headless browser, against a budget, as well as
   natively. Only the electro song has a budget today, and only natively.
3. **Fuzzing**, as the Testing section promises and the suite doesn't yet do: random parameter sweeps
   and note and MIDI streams across all 16 tracks (note-on storms, missing note-offs, panics), each run
   through the health checks.
4. **The accessibility tests** the plan promises: an axe-core audit and a Tab-order walk in the
   browser test.
5. **Re-baselining is deliberate.** A commit with `UPDATE_GOLDEN=1` names the renders it changed and
   why, and the changed ones are listened to. The suite proves that nothing changed, not that it sounds
   good.
6. **Plaits' timing at 44.1 kHz.** The +1.42-semitone correction fixes pitch only: Plaits' envelopes
   and low-pass gate are built for about 47.9 kHz, so they run about 8 % slower here. Measure a
   percussive preset's decay against Plaits at its own rate and correct the time constants in the
   adapter if the difference can be heard.

### Phase 8: what the plan promised

1. **WAV export** from the tracker: the offline render `song_render` already does natively, run in
   WebAssembly, with `RENDERING…` on the display first.
2. **The playlist:** songs queued one after another with a set gap, reorderable, startable anywhere.
3. **The perform view:** the big display as full-screen visuals that react to each track's level.

### Phase 9: the mix

1. **Sidechain ducking** on every mixer channel, keyed from the LXR's kick (or any channel): amount,
   release, and a help line that says what it does. Trance and synthwave rely on it.
2. **A trance gate** as a channel insert next to the filter: a 16-step gate pattern in tempo, with
   depth and smoothing.
3. **An EQ** on every channel: low shelf, a sweepable mid and a high shelf (Phase 3's "Not yet").

### Phase 10: the tracker, for the genres

1. **An effect column** in the cell: a parameter (cutoff, resonance, send, level...) set on a row, with
   a slide to the next value, for the sweeps dub techno and trance live on. Patterns' own effects stay,
   for the jumps on the downbeat.
2. **Swing** per pattern, for electro and dub techno.
3. **Glide and note length** for the mono VA slots, for acid and rubbery bass lines.
4. **Probability and conditions** on a cell (play 50 %, every 2nd time...), so long dub techno loops
   don't sound static.
5. **A MIDI controller:** the registry's CC map mapped to the soft knobs of the device in focus, so a
   physical knob box plays the studio.
6. **Trance, version 3**, with ducking, the gate and the effect column, listened to like the others.

## Testing: as much as possible without a browser or ears

Fel-Tracker's suite showed this works: golden hashes, CPU counts and a layout lint, under 5 minutes.

| Tier | When | Runs | Time |
|---|---|---|---|
| 0 | On save | C unit tests (sequencer timing to the sample, router, registry consistency, preset round-trip); panel lint | < 10 s |
| 1 | Each commit | Golden renders, health checks, CPU budgets, fuzz | < 1 min |
| 2 | Before merge | WebAssembly parity, headless browser smoke, screenshots, accessibility audit | < 5 min |
| 3 | Each phase | You, by ear, from a checklist | Minutes |

**What each kind of test catches:**

- **Golden renders.** Every engine × preset, plus each demo song, is rendered offline natively and hashed.
  A sound change fails with the list of renders it affected. A change that is meant to happen is
  re-baselined in its own commit.
- **Health checks** on every render: NaN or infinity, DC offset, clipping, denormals, silence after the
  release, and all voices free at the end (no stuck notes).
- **CPU budgets.** Nanoseconds per block for each engine and each demo song, compared against a baseline
  (+25% fails). This is the only way to know the one audio thread still fits.
- **Fuzzing.** Random parameter sweeps and random note and MIDI streams, including note-on storms, missing
  note-offs and panics. Every run must pass the health checks.
- **WebAssembly parity.** The WebAssembly build exposes the same offline render function. Its output must
  match the native one, which proves the browser plays what the native tests checked. Native builds use
  `-ffp-contract=off`, so the match is exact except for 1-LSB differences from the two math libraries.
  The test allows those 1-LSB differences.
- **Panel lint, without a browser.** Every panel declaration is checked statically:
  - no overlapping controls and nothing off the panel;
  - every display string fits its cell;
  - every control has a registry parameter, help text and a keyboard path;
  - the LED colours follow the grammar;
  - the whole studio, every device compact and one in focus, fits 1280×800 without scrolling.
- **Headless browser** (Playwright): boot, switch on, play a demo song, then:
  - a screenshot diff of each device;
  - an axe-core accessibility audit;
  - a Tab-order walk through every control.
- **By ear.** Each phase ends with WAV renders and a checklist doc (keep / change), like the Fel-Tracker test
  plan.

## Decisions (2026-10-25)

1. **Pattern sharing:** not at first. Patterns live inside songs, as in TRIS.
2. **Tracks:** 16, shown 8 at a time.
3. **Cell:** the TRIS cell (up to 4 notes, velocity) plus RETRIG.
4. **LXR:** you can play the LXR alone. Its sequencer takes the clock when the tracker is off.
5. **Layout:** the whole studio fits on one desktop page, and you can move between devices. Phones are not a
   priority.

## Later

Added on 2026-10-26, after the phases above. **Nothing here starts while a listening checklist still has
empty keep/change lines:** tuning what exists makes better songs than another engine.

- **Mutable Instruments Rings**, the resonator (MIT, from the same eurorack repository as Plaits): a
  device of its own, played from the tracker, and possibly fed by other channels.
- **Surge XT's waveforms**: its oscillators (classic, modern, wavetable, window, sine, FM and the
  others) as an instrument, included sound design, unlike Dexed. To reconsider first: full sound design
  goes against "understandable without a manual" and Dexed's presets-only decision.
- **Mutable Instruments Clouds or Beads** (granular, MIT), for IDM.
- **Surge XT's waveshapers** (sst-waveshapers, already fetched): soft and hard clippers, folders,
  asymmetric and digital shapers, as an insert on a channel next to its filter.
- **Before hosting the studio anywhere public** (even a shared page): `NOTICE.md` forbids using "LXR"
  and "Erica Synths" in a way that suggests the work comes from them, so rename the LXR device; publish
  the source, as GPL-3 requires; and check that Dexed's 33 cartridges may be redistributed, or ship
  only those that may. Private use needs none of this.
