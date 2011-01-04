/*
*		libfaded -0.1 alpha
* 
*		note: this has never been compiled, its a brain dump in c
*
*		libfaded is a set of functions to prevent the harshing of mellows caused by popping/chunking 
*		sounds on the pausing, resuming, stopping, seeking and track jumping of music or any other audio
*		prone to abrupt starting stopping or changing. 
*
*		It is able to fade in or out from silence or fade between two tracks in a linear way
*		It is designed to work either with the entire peice of audio you desire to be faded or
*		with any chunk thereof 
*
*		intended usage examples (whole peices):
*
*									 crossfade(from_buffer, to_buffer, mixed_buffer, int sample_count)
*									 		reads from two buffers, writing to a third
*
*									 fade_in(buffer, int sample_count)
*											writes the faded samples to the buffer provided										
*	
*		partial examples:
*
*						fade_out_chunk(sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples)
*							writes to the buffer provided
*
*						crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples)
*									 		reads from two buffers, writing to a third
*
*
*		Todo: Compile and test, make sample type independent, and probably channel count indepentant
*		Future: Nonlinear fades? Something like jquery easing perhaps?
*
*/

#include <math.h>

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
int crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples);


/* ye functions */

float
get_fade_amount_per_sample(int sample_count) {

	return -60.0f / (float)sample_count;

}


 float
get_crossfade_amount_per_sample(int sample_count) {

	return 2.0f / (float)sample_count;

}

float db_to_value(float db)
{
  /* 20 log rule */
  return powf(10.0, db/20.0);
}


int
crossfade(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_count) {

	return crossfade_chunk(sample_buffer_from, sample_buffer_to, faded_sample_buffer, 0, sample_count, sample_count);

}



int
fade_in(void *sample_buffer, int sample_count) {

	return fade_chunk(sample_buffer, 0, sample_count, sample_count, 1);

}

int
fade_out(void *sample_buffer, int sample_count) {

	return fade_chunk(sample_buffer, 0, sample_count, sample_count, 0);

}

int
fade_in_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	return fade_chunk(sample_buffer, sample_start_number, samples_in_chunk, total_samples, 1);

}


int
fade_out_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	return fade_chunk(sample_buffer, sample_start_number, samples_in_chunk, total_samples, 0);

}

int
fade_chunk(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out) {

	/* ok lets handle that void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	float *samples = (float*)sample_buffer;

	int current_sample_number, number_of_channels, i ,j;
	float fade_amount_per_sample, current_fade_amount;
	
	/* well as with the sample type we dont really want to hard code the number of channels */
	number_of_channels = 2;

	current_sample_number = sample_start_number;
	fade_amount_per_sample = get_fade_amount_per_sample(total_samples);


	for (i = 0; i < samples_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {

			/* 0 is fade out, 1 is fade in */
			if(in_or_out) {	
				current_fade_amount = db_to_value(fade_amount_per_sample * (total_samples - current_sample_number));
			} else {
				current_fade_amount = db_to_value(fade_amount_per_sample * current_sample_number);
			}

			samples[i*number_of_channels + j] = samples[i*number_of_channels + j] * current_fade_amount;

			current_sample_number += 1;

		}
	}

	return 0;
}

int
crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	float *samples_from = (float*)sample_buffer_from;
	float *samples_to = (float*)sample_buffer_to;
	float *faded_samples = (float*)faded_sample_buffer;

	int current_sample_number;
	float crossfade_amount_per_sample, current_crossfade_amount, crossfade_starting_amount, coef_from, coef_to;
	int number_of_channels, i, j; /* , bytes; */
	/* well as with the sample type we dont really want to hard code the number of channels */
	number_of_channels = 2;

	/* bytes	= samples_in_chunk * number_of_channels * sizeof (float); */

	crossfade_starting_amount = -1.0f;

	current_sample_number = sample_start_number;
	crossfade_amount_per_sample = get_crossfade_amount_per_sample(total_samples);



	/* bytes /= number_of_channels * sizeof (float); */
	for (i = 0; i < samples_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {

			current_crossfade_amount = crossfade_starting_amount + ((float)current_sample_number * crossfade_amount_per_sample);

			coef_to = (current_crossfade_amount + 1.0f) * 0.5f;
			coef_from = 1.0f - coef_to;

			/* this was faded_samples[j][i]  hrmz0r?? */
			faded_samples[i*number_of_channels + j] = samples_from[i*number_of_channels + j] * coef_from + samples_to[i*number_of_channels + j] * coef_to;

			current_sample_number += 1;
		
		}
	}

	return 0;

}
