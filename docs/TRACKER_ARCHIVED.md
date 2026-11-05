# The tracker, archived (2026-11-05)

Orange Mouse is now played by an external sequencer (web/app/devices/midi.js,
studio/midi_in.c). The tracker's panel is out of the studio until we decide how it
comes back. Nothing was deleted, and no song was touched.

## What is archived

| What | Where it is now |
|---|---|
| The panel: display, editing, song list, playlist, WAV export | `web/app/archive/tracker-device.js` |
| Its browser checks (not run) | `tests/archive/tracker_smoke_sections.txt` |
| Its screenshot | `tests/archive/tracker.png` |

## What stays in use

- **The C tracker** (`studio/tracker.c`) stays in the studio, stopped, and is still loaded
  with each song. It is what plays the genre songs in `tests/run.sh`: those renders are the
  golden hashes that protect every engine's sound.
- **The songs** (`web/app/tracker/`): the library, the song format, the genre demos and the
  backup folder. A song is now also a *sound set*: its sounds and mix, loaded with SET < and
  SET > on the MIDI device. Its patterns are kept and not played.

## What you can't do while it is archived

Write or play patterns inside the studio; browse, rename, delete, import or export songs;
share codes; the playlist; WAV export. The perform view stays (F, or PERF), and shows
levels and the tempo rather than the pattern and row.

## Bringing it back

1. Move `web/app/archive/tracker-device.js` back to `web/app/devices/tracker.js`.
2. In `web/app/main.js`, create it with `createTrackerDevice`, add it to `devices`, route
   its keys, and send the worklet's tracker state to `onState` (the worklet's `sendUi` must
   send `om_web_tracker_state()` again). It keeps its own song: make it use
   `web/app/session.js` instead of its own `song`, `send` and `keepLxrKit`.
3. Put the checks back in `tests/browser/smoke.mjs` and the screenshot in
   `tests/golden/screens/`.

## Questions for later

- **Who is the master clock?** The tracker was; now MIDI clock is. The two tick the LXR's
  sequencer and the gates through the same `om_studio_clock`, so they must not run together.
  Either choose one in the panel, or let the tracker follow MIDI clock (it would need to
  advance by ticks rather than by its own tempo).
- **What is a song then?** Patterns and sound sets could split: an external sequencer only
  needs the sound set.
- **Send MIDI out?** A tracker that sends notes could drive outside gear too.
