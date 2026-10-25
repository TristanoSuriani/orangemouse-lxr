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
   - Plaits is the only engine designed for 48 kHz. Its adapter corrects its pitch (+1.47 semitones), or
     resamples that one voice (Phase 5).
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
- **Not yet:** effects as inserts on a channel, and an EQ.

### Phase 4: Dexed

Presets only: a bank browser on a character display, and volume and transpose on soft knobs.

### Phase 5: Plaits

The genre-picked engines, with presets for each genre.

### Phase 6: genre packs

Presets and one demo song per genre for trance, electro, synthwave and dub techno. These demo songs also
become the regression songs. IDM comes later.

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
