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

typedef enum xmms_fader_status_E {
	INACTIVE,
	FADING_OUT,
	FADING_IN,
} xmms_fader_status_t;

typedef struct xmms_fader_St {
	xmms_fader_status_t status;
	int current_frame_number;
	int total_frames;
	int current_fade_amount[128];
	int lastsign[128];
	xmms_stream_type_t *format;
} xmms_fader_t;

float next_fade_amount(xmms_fader_t *fader, int i );
void fade_slice(xmms_fader_t *fader, void *buffer, int len);




int crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s16(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);


