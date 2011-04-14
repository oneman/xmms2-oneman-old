/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2011 XMMS2 Team
 *
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include "xmmspriv/xmms_ringbuf.h"

typedef enum xmms_fader_status_E {
	INACTIVE,
	FADING_OUT,
	FADING_IN,
} xmms_fader_status_t;

typedef struct xmms_fader_St {
	xmms_fader_status_t status;
	int current_frame_number;
	int total_frames;
	int final_frame[128];
	float current_fade_amount[128];
	int lastsign[128];
	xmms_stream_type_t *format;
	int callback;
} xmms_fader_t;

typedef struct xmms_playback_transition_St {
	xmms_fader_status_t status;
	int current_frame_number;
	int total_frames;
	xmms_stream_type_t *format;
} xmms_playback_transition_t;

typedef struct xmms_xtransition_St {
	gboolean setup;
	int current_frame_number;
	int total_frames;
	int final_frame[2][128];
	float current_fade_amount[2][128];
	int lastsign[2][128];
	//xmms_transition_state_t *transition;
	xmms_stream_type_t *format;
	xmms_ringbuf_t *outring;	// first source
	xmms_ringbuf_t *inring;		// second source
	gboolean readlast;
	void *last;
} xmms_xtransition_t; 

typedef enum xmms_transition_state_E {
	NONE,
	PAUSING,
	RESUMING,
	STARTING,
	STOPPING,
	SEEKING,
	ADVANCING,
	JUMPING,
	SWAP,
} xmms_transition_state_t;

int find_final_zero_crossing (void *buffer, int len);

void fade_slice(xmms_fader_t *fader, void *buffer, int len);

int crossfade_slice(xmms_xtransition_t *transition, void *buffer, int len);

int crossfade_slice_float(xmms_xtransition_t *transition, void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int len);
int crossfade_slice_s16(xmms_xtransition_t *transition, void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int len);
int crossfade_slice_s32(xmms_xtransition_t *transition, void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int len);


