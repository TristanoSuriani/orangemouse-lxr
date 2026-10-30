# Golden changes

Each re-baseline: why, and which renders changed (PLAN.md Phase 7.5). Listen to those.

## 2026-10-30: the tracker's display says when songs aren't safe (Phase 7.1)

- `screens/tracker.png`: the order row shows SONGS NOT KEPT FOR SURE (headless Chrome refuses persistent storage)

## 2026-10-30: the LXR's oscillators wrap at the end of their waves (Phase 7.3, found by the fuzz)

- `lxr_params.txt`: 729 of 1460 renders: every render of the "busy" kit. Peak and DC unchanged to 4 decimals: one sample per cycle now interpolates towards the wave's own start, not the next wave's.

## 2026-10-30: Plaits runs at its own 47872 Hz, resampled to 44.1 kHz: its envelopes and engines were 6-9 % slow (Phase 7.6). Listen to the Plaits parts

- `studio_demo.txt`: 1 of 1 renders: (the file)
- `songs.txt`: 3 of 3 renders: electro, synthwave, dubtechno
- `stress.txt`: 1 of 1 renders: (the file)

## 2026-10-30: the tracker gets a PERF pad for the perform view (Phase 8.3); its pads sit 6 px apart

- `screens/tracker.png` (18436 pixels)
