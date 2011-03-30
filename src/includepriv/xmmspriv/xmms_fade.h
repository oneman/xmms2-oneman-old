typedef struct xmms_fader_St {
	int active;  /* 0 = none, 1 = out, 2 = in  */
	int current_frame_number;
	int total_frames;
	
	int current_fade_amount[128];
	int lastsign[128];
	
} xmms_fader_t;



float next_fade_amount(xmms_fader_t *fader, int i );

int fade_chunk(void *sample_buffer, xmms_fader_t *fader, xmms_stream_type_t *format, int framecount);

int fade_chunk_s16(void *sample_buffer, xmms_fader_t *fader, int framecount);
int fade_chunk_s32(void *sample_buffer, xmms_fader_t *fader, int framecount);
int fade_chunk_float(void *sample_buffer, xmms_fader_t *fader, int framecount);



int crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s16(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);


