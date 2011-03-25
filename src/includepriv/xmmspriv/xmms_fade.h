/* ye prototypes */

float get_fade_amount_per_sample(int sample_count);
float get_crossfade_amount_per_sample(int sample_count);
float db_to_value(float db);
int crossfade(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_count);
int fade_in(void *sample_buffer, int sample_count);
int fade_out(void *sample_buffer, int sample_count);
int fade_in_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int fade_out_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int fade_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out);
int fade_chunk_s16(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out);
int fade_chunk_s32(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out);
int crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);

int crossfade_chunk_s16(void *sample_buffer_from, int fadeoffset, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);
int crossfade_chunk_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);

