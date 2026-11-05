/*
 * midi_in.h
 *
 * An external sequencer plays the studio: the MIDI input's router and clock
 * follower. It takes whole MIDI messages stamped with the studio frame they
 * should sound on (the page turns the browser's timestamps into frames) and,
 * in that order:
 *
 *  - routes each channel's notes to one engine's part, or to the raw sink (the
 *    LXR firmware's own MIDI settings), or nowhere. Everything else that is not
 *    a clock message goes to the raw sink, so the firmware's CCs and program
 *    changes work as on the hardware.
 *  - follows MIDI clock: Start, Continue, Stop and the ticks go to the studio's
 *    clock (the LXR's sequencer, the mixer's gates), and the tempo measured from
 *    the ticks goes to the engines and the send effects. Ticks also arrive while
 *    stopped: they give the tempo but move nothing.
 *  - remembers the notes it started, so a Stop, an All Notes Off, a re-route or
 *    a lost port never leaves one hanging.
 *
 * It runs on the audio thread and does not allocate after creation.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_MIDI_IN_H_
#define OM_MIDI_IN_H_

#include <stdint.h>
#include "studio.h"

#define OM_MIDI_CHANNELS 16
#define OM_MIDI_OFF 0xFF   // route: the channel's notes are ignored
#define OM_MIDI_RAW 0xFE   // route: the channel's notes go to the raw sink

typedef struct OmMidiIn OmMidiIn;

// raw: receives bytes for the LXR firmware (NULL: they are dropped).
OmMidiIn *om_midi_in_create(OmStudio *st, float sampleRate, void (*raw)(uint8_t byte));
void om_midi_in_destroy(OmMidiIn *m);

// Where a channel (0-15) plays; every channel starts off. Its held notes end.
void om_midi_in_route(OmMidiIn *m, uint8_t channel, uint8_t engine, uint8_t part);

// The tempo while no clock arrives; a running clock's tempo wins.
void om_midi_in_tempo(OmMidiIn *m, float bpm);

// One whole message, in the order it arrived. The order is kept: a message
// stamped before the one ahead of it takes that one's frame. A frame already
// rendered plays at once and counts as late.
void om_midi_in_event(OmMidiIn *m, uint64_t frame, uint8_t b0, uint8_t b1, uint8_t b2);

// Applies the messages due in the next `frames` frames: call it before
// om_studio_render.
void om_midi_in_advance(OmMidiIn *m, uint32_t frames);

// The port went away: stops the clock, ends the notes, forgets what is queued.
void om_midi_in_reset(OmMidiIn *m);

typedef struct
{
	uint8_t running;    // between a Start or Continue and a Stop
	uint8_t locked;     // a clock tick in the last second
	float bpm;          // the tempo the studio runs at
	uint32_t late;      // messages that arrived too late to play on time
	float lateMs;       // the latest of them
	uint32_t dropped;   // messages lost to a full queue
	uint16_t activity;  // a bit per channel: a note since the last call
} OmMidiInState;

// Where it stands; reading clears `activity`.
OmMidiInState om_midi_in_state(OmMidiIn *m);

#endif /* OM_MIDI_IN_H_ */
