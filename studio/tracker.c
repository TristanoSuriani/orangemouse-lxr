/*
 * tracker.c
 *
 * See tracker.h. Playback keeps the frame (fractional, so tempos that don't
 * divide the sample rate don't drift) at which the next row starts; advance()
 * plays every row starting before the end of the block, at its exact frame.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdlib.h>
#include <string.h>
#include "tracker.h"

typedef struct
{
	OmCell cell[OM_MAX_ROWS][OM_TRACKS];
	uint8_t rows;
	float bpm;   // 0: the song's
} Pattern;

typedef struct
{
	uint8_t engine, part;
	uint8_t routed;
	uint8_t muted, solo;
	uint8_t sounding[OM_CHORD];   // notes playing, to release
	uint8_t count;                // how many
} Track;

struct OmTracker
{
	OmStudio *studio;
	float rate, bpm;
	Pattern pattern[OM_PATTERNS];
	uint8_t order[OM_ORDER_LEN];
	Track track[OM_TRACKS];

	// playback
	uint8_t playing, loop;
	uint8_t current;       // the pattern playing
	uint16_t position;     // in the order list
	int16_t row;           // the next row to start
	uint8_t lastRow;
	double nextFrame;      // when it starts
};

static const OmCell EMPTY = {{OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY}, 0, 0};

OmTracker *om_tracker_create(OmStudio *studio, float sampleRate)
{
	OmTracker *t = calloc(1, sizeof(OmTracker));
	if (!t)
		return NULL;
	t->studio = studio;
	t->rate = sampleRate;
	om_tracker_clear(t);
	return t;
}

void om_tracker_destroy(OmTracker *t)
{
	free(t);
}

void om_tracker_clear(OmTracker *t)
{
	for (int p = 0; p < OM_PATTERNS; p++)
	{
		for (int r = 0; r < OM_MAX_ROWS; r++)
			for (int k = 0; k < OM_TRACKS; k++)
				t->pattern[p].cell[r][k] = EMPTY;
		t->pattern[p].rows = 64;
		t->pattern[p].bpm = 0.f;
	}
	memset(t->order, OM_ORDER_END, sizeof(t->order));
	t->order[0] = 0;
	t->bpm = 120.f;
}

void om_tracker_set_cell(OmTracker *t, uint8_t pattern, uint8_t row, uint8_t track, const OmCell *cell)
{
	if (pattern < OM_PATTERNS && row < OM_MAX_ROWS && track < OM_TRACKS)
		t->pattern[pattern].cell[row][track] = *cell;
}

const OmCell *om_tracker_cell(const OmTracker *t, uint8_t pattern, uint8_t row, uint8_t track)
{
	return pattern < OM_PATTERNS && row < OM_MAX_ROWS && track < OM_TRACKS ? &t->pattern[pattern].cell[row][track] : &EMPTY;
}

void om_tracker_set_length(OmTracker *t, uint8_t pattern, uint8_t rows)
{
	if (pattern < OM_PATTERNS && rows >= 1 && rows <= OM_MAX_ROWS)
		t->pattern[pattern].rows = rows;
}

void om_tracker_set_pattern_tempo(OmTracker *t, uint8_t pattern, float bpm)
{
	if (pattern < OM_PATTERNS)
		t->pattern[pattern].bpm = bpm;
}

void om_tracker_set_order(OmTracker *t, uint16_t position, uint8_t pattern)
{
	if (position < OM_ORDER_LEN)
		t->order[position] = pattern < OM_PATTERNS ? pattern : OM_ORDER_END;
}

void om_tracker_set_tempo(OmTracker *t, float bpm)
{
	if (bpm >= 20.f && bpm <= 400.f)
		t->bpm = bpm;
}

void om_tracker_route(OmTracker *t, uint8_t track, uint8_t engine, uint8_t part)
{
	if (track >= OM_TRACKS)
		return;
	t->track[track].engine = engine;
	t->track[track].part = part;
	t->track[track].routed = om_studio_engine(t->studio, engine) != NULL;
}

void om_tracker_set_mute(OmTracker *t, uint8_t track, uint8_t muted)
{
	if (track < OM_TRACKS)
		t->track[track].muted = muted != 0;
}

void om_tracker_set_solo(OmTracker *t, uint8_t track, uint8_t solo)
{
	if (track < OM_TRACKS)
		t->track[track].solo = solo != 0;
}

// ---- playback ----

static float tempoOf(const OmTracker *t, uint8_t pattern)
{
	return t->pattern[pattern].bpm > 0.f ? t->pattern[pattern].bpm : t->bpm;
}

static double rowFrames(const OmTracker *t)
{
	return t->rate * 60.0 / tempoOf(t, t->current) / 4.0;
}

static void release(OmTracker *t, uint8_t k, uint64_t frame)
{
	Track *tr = &t->track[k];
	for (uint8_t i = 0; i < tr->count; i++)
		om_studio_note_off(t->studio, frame, tr->engine, tr->part, tr->sounding[i]);
	tr->count = 0;
}

static uint8_t audible(const OmTracker *t, uint8_t k)
{
	uint8_t anySolo = 0;
	for (int i = 0; i < OM_TRACKS; i++)
		anySolo |= t->track[i].solo;
	return t->track[k].routed && !t->track[k].muted && (!anySolo || t->track[k].solo);
}

// Plays one row of the current pattern at `frame`.
static void playRow(OmTracker *t, uint8_t row, uint64_t frame, double length)
{
	for (uint8_t k = 0; k < OM_TRACKS; k++)
	{
		const OmCell *c = &t->pattern[t->current].cell[row][k];
		if (c->note[0] == OM_NOTE_EMPTY)
			continue;
		Track *tr = &t->track[k];
		release(t, k, frame);
		if (c->note[0] == OM_NOTE_OFF || !audible(t, k))
			continue;
		const uint8_t vel = c->velocity ? c->velocity : 100;
		const uint8_t hits = c->retrig >= 2 && c->retrig <= 8 ? c->retrig : 1;
		for (uint8_t h = 0; h < hits; h++)
		{
			const uint64_t at = frame + (uint64_t)(h * length / hits);
			if (h > 0)
				release(t, k, at);   // each hit ends the one before
			for (uint8_t i = 0; i < OM_CHORD && c->note[i] < 128; i++)
			{
				om_studio_note_on(t->studio, at, tr->engine, tr->part, c->note[i], vel);
				tr->sounding[tr->count++] = c->note[i];
			}
		}
	}
}

// Moves to the next row: the next pattern in the order list at a pattern's
// end, or stops at the song's end.
static void nextRow(OmTracker *t)
{
	if (++t->row < t->pattern[t->current].rows)
		return;
	t->row = 0;
	if (t->loop)
		return;
	t->position++;
	if (t->position >= OM_ORDER_LEN || t->order[t->position] == OM_ORDER_END)
	{
		t->playing = 0;   // the song is over: it does not wrap
		return;
	}
	t->current = t->order[t->position];
}

void om_tracker_advance(OmTracker *t, uint32_t frames)
{
	const uint64_t now = om_studio_frame(t->studio);
	const uint64_t end = now + frames;
	while (t->playing && t->nextFrame < (double)end)
	{
		const uint64_t at = t->nextFrame < (double)now ? now : (uint64_t)(t->nextFrame + 0.5);
		const double length = rowFrames(t);
		t->lastRow = (uint8_t)t->row;
		playRow(t, (uint8_t)t->row, at, length);
		t->nextFrame += length;
		nextRow(t);
		if (!t->playing)
			for (uint8_t k = 0; k < OM_TRACKS; k++)
				release(t, k, (uint64_t)(t->nextFrame + 0.5));
	}
}

static void start(OmTracker *t)
{
	for (uint8_t k = 0; k < OM_TRACKS; k++)
		release(t, k, om_studio_frame(t->studio));
	t->row = 0;
	t->lastRow = 0;
	t->nextFrame = (double)om_studio_frame(t->studio);
	t->playing = 1;
}

void om_tracker_play_song(OmTracker *t, uint16_t position)
{
	if (position >= OM_ORDER_LEN || t->order[position] == OM_ORDER_END)
		return;
	t->loop = 0;
	t->position = position;
	t->current = t->order[position];
	start(t);
}

void om_tracker_play_pattern(OmTracker *t, uint8_t pattern)
{
	if (pattern >= OM_PATTERNS)
		return;
	t->loop = 1;
	t->current = pattern;
	start(t);
}

void om_tracker_stop(OmTracker *t)
{
	t->playing = 0;
	for (uint8_t k = 0; k < OM_TRACKS; k++)
		release(t, k, om_studio_frame(t->studio));
}

OmTrackerState om_tracker_state(const OmTracker *t)
{
	return (OmTrackerState){
		.playing = t->playing,
		.pattern = t->current,
		.position = t->position,
		.row = t->lastRow,
		.bpm = tempoOf(t, t->current),
	};
}
