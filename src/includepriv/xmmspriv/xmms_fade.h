typedef struct xmms_fader_St {
	int active;  /* 0 = none, 1 = out, 2 = in  */
	int current_frame_number;
	int total_frames;
} xmms_fader_t;

float get_fade_amount_per_sample(int sample_count);
float get_crossfade_amount_per_sample(int sample_count);
float db_to_value(float db);
int crossfade(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_count);
int fade_in(void *sample_buffer, int sample_count);
int fade_out(void *sample_buffer, int sample_count);
int fade_in_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int fade_out_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int fade_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out);
int fade_chunk_s16(void *sample_buffer, xmms_fader_t *fader, int framecount);
int fade_chunk_s32(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out);
int crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);

int crossfade_chunk_s16(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);



int fade_chunk_new(void *sample_buffer, xmms_fader_t *fader, xmms_stream_type_t *format, int framecount);

