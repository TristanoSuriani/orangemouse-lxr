/*
 * tracker.h
 *
 * The tracker's sequencer core (PLAN.md Phase 2, step 1): the song and its
 * playback, on the audio thread. The tracker is the studio's clock: before
 * each block, om_tracker_advance() turns the rows that start in that block
 * into notes on the studio's router, on their exact frames.
 *
 *  - 16 tracks. Each plays one engine's part (an LXR voice, a VA slot...).
 *  - Patterns of 1..128 rows; a row is a sixteenth note. Each pattern has its
 *    own tempo (0: the song's).
 *  - A cell holds up to 4 notes (a chord) or a note off, a velocity, and a
 *    retrigger: 2..8 hits spread evenly over the row.
 *  - A note sounds until the track's next note or note off; a chord's notes
 *    end together.
 *  - The order list chains patterns. Song mode plays it from a position and
 *    stops at its end (it does not wrap); loop mode repeats one pattern.
 *  - It sends the studio's clock while it plays: start, 6 ticks a row (24 a
 *    beat), stop. The LXR's own sequencer follows it.
 *
 * The page owns the song (editing, undo, the library) and sends each change
 * here; this copy is what plays.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_TRACKER_H_
#define OM_TRACKER_H_

#include <stdint.h>
#include "studio.h"

#define OM_TRACKS        16
#define OM_PATTERNS      64
#define OM_MAX_ROWS      128
#define OM_ORDER_LEN     256
#define OM_CHORD         4

#define OM_NOTE_EMPTY    0xFF
#define OM_NOTE_OFF      0xFE
#define OM_ORDER_END     0xFF

// A cell's effect (PLAN.md 10.1): one of the track's mixer controls set at the
// row, OM_FX_SLIDE: gliding from there to the track's next effect on the same
// control in the pattern. Stopping puts the song's own values back.
enum { OM_FX_NONE, OM_FX_LEVEL, OM_FX_PAN, OM_FX_SEND_A, OM_FX_SEND_B, OM_FX_CUTOFF, OM_FX_RESONANCE, OM_FX_COUNT };
#define OM_FX_SLIDE 0x80

typedef struct
{
	uint8_t note[OM_CHORD];   // MIDI notes, OM_NOTE_EMPTY, or note[0] = OM_NOTE_OFF
	uint8_t velocity;         // 1..127; 0: 100
	uint8_t retrig;           // 0 or 1: one hit; 2..8 hits in the row
	uint8_t fx;               // OM_FX_*, with OM_FX_SLIDE
	uint8_t fxValue;          // 0..127
	uint8_t length;           // the notes end after this many quarter rows (1..64); 0: at the next
	uint8_t glide;            // 1: the notes start over the ones before, which end after: legato,
	                          // a mono synth glides (PLAN.md 10.3)
} OmCell;

typedef struct OmTracker OmTracker;

OmTracker *om_tracker_create(OmStudio *studio, float sampleRate);
void om_tracker_destroy(OmTracker *t);

// ---- the song (from the page) ----
void om_tracker_clear(OmTracker *t);
void om_tracker_set_cell(OmTracker *t, uint8_t pattern, uint8_t row, uint8_t track, const OmCell *cell);
const OmCell *om_tracker_cell(const OmTracker *t, uint8_t pattern, uint8_t row, uint8_t track);
void om_tracker_set_length(OmTracker *t, uint8_t pattern, uint8_t rows);     // 1..OM_MAX_ROWS
void om_tracker_set_pattern_tempo(OmTracker *t, uint8_t pattern, float bpm); // 0: the song's
// Swing (PLAN.md 10.2): the share of each pair of rows the first one takes,
// 50 (straight) to 75 % (a dotted feel)
void om_tracker_set_pattern_swing(OmTracker *t, uint8_t pattern, uint8_t percent);
void om_tracker_set_order(OmTracker *t, uint16_t position, uint8_t pattern); // OM_ORDER_END ends the song
void om_tracker_set_tempo(OmTracker *t, float bpm);                          // the song's
void om_tracker_route(OmTracker *t, uint8_t track, uint8_t engine, uint8_t part);
void om_tracker_set_mute(OmTracker *t, uint8_t track, uint8_t muted);
void om_tracker_set_solo(OmTracker *t, uint8_t track, uint8_t solo);

// ---- transport ----
// Song mode from an order position, or loop one pattern; both from row 0.
void om_tracker_play_song(OmTracker *t, uint16_t position);
void om_tracker_play_pattern(OmTracker *t, uint8_t pattern);
void om_tracker_stop(OmTracker *t);   // releases every sounding note

// Schedules the notes of the next `frames` frames (from the studio's current
// frame) on the router. Call it before om_studio_render() with the same count.
void om_tracker_advance(OmTracker *t, uint32_t frames);

typedef struct
{
	uint8_t playing;      // 0 when stopped (or the song ended)
	uint8_t pattern;
	uint16_t position;    // in the order list (song mode)
	uint8_t row;          // the row that last started
	float bpm;            // the tempo playing
} OmTrackerState;

OmTrackerState om_tracker_state(const OmTracker *t);

#endif /* OM_TRACKER_H_ */
