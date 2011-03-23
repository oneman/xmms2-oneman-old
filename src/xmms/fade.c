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
#include "xmms/xmms_log.h"

#include "xmmspriv/xmms_fade.h"

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

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels;
	number_of_channels = 2;
	
	total_samples = total_samples / number_of_channels;
	

	int current_sample_number, i ,j;
	int sign[number_of_channels], lastsign[number_of_channels];
	float fade_amount_per_sample, next_fade_amount;
	float current_fade_amount[number_of_channels];
	
	for (j = 0; j < number_of_channels; j++) {
		current_fade_amount[j] = 0;
		sign[j] = 0;
		lastsign[j] = 0;
	}
	current_sample_number = sample_start_number / number_of_channels;
	fade_amount_per_sample = get_fade_amount_per_sample(total_samples);


	for (i = 0; i < samples_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {

			/* 0 is fade out, 1 is fade in */
			if(in_or_out) {	
				next_fade_amount = db_to_value(fade_amount_per_sample * (total_samples - current_sample_number));
			} else {
				next_fade_amount = db_to_value(fade_amount_per_sample * current_sample_number);
			}
			
			if (current_fade_amount[j] == 0)
				current_fade_amount[j] = next_fade_amount;
			
			if (samples[i*number_of_channels + j] >= 0) {
				sign[j] = 1;
			} else {
				sign[j] = 0;
			}
			
			if (sign[j] != lastsign[j]) {
				current_fade_amount[j] = next_fade_amount;
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
			}


			samples[i*number_of_channels + j] = samples[i*number_of_channels + j] * current_fade_amount[j];

			lastsign[j] = sign[j];

		}
		
		current_sample_number += 1;
	}

	return 0;
}

int
fade_chunk_s16(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out) {

	/* ok lets handle that void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint16 *samples = (gint16*)sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_chunk, total_frames;
	number_of_channels = 2;
	
	frames_in_chunk = samples_in_chunk / number_of_channels;
	total_frames = total_samples / number_of_channels;
	

	int i ,j;
	int sign[number_of_channels], lastsign[number_of_channels];
	float next_fade_amount;
	gint32 current_fade_amount[number_of_channels];
	
	for (j = 0; j < number_of_channels; j++) {
		current_fade_amount[j] = 0;
		sign[j] = 0;
		lastsign[j] = -1;
	}
	
	int fuckit;
	fuckit = 0;
	
	total_frames = total_frames / 2;
	if(sample_start_number >= ((total_frames * 2)))
		fuckit = 1;

	for (i = 0; i < frames_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {

			/* 0 is fade out, 1 is fade in */
			if(in_or_out) {	
				next_fade_amount = ((float)(i + (sample_start_number / number_of_channels))* 100.0)/(float)total_frames;
				next_fade_amount = next_fade_amount/10.0;
				next_fade_amount = next_fade_amount * next_fade_amount;
				if (fuckit == 1) {
					next_fade_amount = 100;
				}
			} else {
				next_fade_amount = ((float)(total_frames - (i + (sample_start_number / number_of_channels)))* 100.0)/(float)total_frames;
				next_fade_amount = next_fade_amount/10.0;
				next_fade_amount = next_fade_amount * next_fade_amount;
				if (fuckit == 1) {
					next_fade_amount = 0;
				}
			}
			
			if (current_fade_amount[j] == 0)
				current_fade_amount[j] = next_fade_amount;
			
			if (samples[i*number_of_channels + j] >= 0) {
				sign[j] = 1;
			} else {
				sign[j] = 0;
			}
			
			if(lastsign[j] == -1)
				lastsign[j] = sign[j];
			
			if (sign[j] != lastsign[j]) {
				current_fade_amount[j] = next_fade_amount;
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
				if(current_fade_amount[j] < 0)
					current_fade_amount[j] = 0;
				if(current_fade_amount[j] > 100)
					current_fade_amount[j] = 100;
			}

		/*	if (i*number_of_channels + j == 515)	{
			printf("i: %d total frames: %d", i, total_frames);
							next_fade_amount = ((float)i * 100.0)/(float)total_frames;
				printf("fade percent %f", next_fade_amount);
				next_fade_amount = next_fade_amount/100.0;
				next_fade_amount = next_fade_amount * next_fade_amount;

			}*/
			if (i*number_of_channels + j == frames_in_chunk)	{
				printf("Current Sample: %d current fade amount: %d result ::: %d :::: \n", samples[i*number_of_channels + j], current_fade_amount[j],  ((samples[i*number_of_channels + j] * current_fade_amount[j]) / 100));
			}
			samples[i*number_of_channels + j] = ((samples[i*number_of_channels + j] * current_fade_amount[j]) / 100);

			lastsign[j] = sign[j];

		}
		
	}

	return 0;
}


int
fade_chunk_s32(void *sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples, int in_or_out) {

	/* ok lets handle that void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint32 *samples = (gint32*)sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_chunk, total_frames;
	number_of_channels = 2;
	
	frames_in_chunk = samples_in_chunk / number_of_channels;
	total_frames = total_samples / number_of_channels;


	int i ,j;
	int sign[number_of_channels], lastsign[number_of_channels];
	float next_fade_amount;
	gint64 current_fade_amount[number_of_channels];
	
	for (j = 0; j < number_of_channels; j++) {
		current_fade_amount[j] = 0;
		sign[j] = 0;
		lastsign[j] = -1;
	}

	int fuckit;
	fuckit = 0;

	total_frames = total_frames / 2;
	if(sample_start_number >= ((total_frames * 2)))
		fuckit = 1;

	for (i = 0; i < frames_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {

			/* 0 is fade out, 1 is fade in */
			if(in_or_out) {	
				next_fade_amount = ((float)(i + (sample_start_number / number_of_channels))* 100.0)/(float)total_frames;
				next_fade_amount = next_fade_amount/10.0;
				next_fade_amount = next_fade_amount * next_fade_amount;
				if (fuckit == 1) {
					next_fade_amount = 100;
				}
			} else {
				next_fade_amount = ((float)(total_frames - (i + (sample_start_number / number_of_channels)))* 100.0)/(float)total_frames;
				next_fade_amount = next_fade_amount/10.0;
				next_fade_amount = next_fade_amount * next_fade_amount;
				if (fuckit == 1) {
					next_fade_amount = 0;
				}
			}
			
			if (current_fade_amount[j] == 0)
				current_fade_amount[j] = next_fade_amount;
			
			if (samples[i*number_of_channels + j] >= 0) {
				sign[j] = 1;
			} else {
				sign[j] = 0;
			}
			
			if(lastsign[j] == -1)
				lastsign[j] = sign[j];
			
			if (sign[j] != lastsign[j]) {
				current_fade_amount[j] = next_fade_amount;
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
				if(current_fade_amount[j] < 0)
					current_fade_amount[j] = 0;
				if(current_fade_amount[j] > 100)
					current_fade_amount[j] = 100;
			}

		/*	if (i*number_of_channels + j == 515)	{
			printf("i: %d total frames: %d", i, total_frames);
							next_fade_amount = ((float)i * 100.0)/(float)total_frames;
				printf("fade percent %f", next_fade_amount);
				next_fade_amount = next_fade_amount/100.0;
				next_fade_amount = next_fade_amount * next_fade_amount;

			}*/
			if (i*number_of_channels + j == frames_in_chunk)	{
				printf("Current Sample: %d current fade amount: %d result ::: %d :::: \n", samples[i*number_of_channels + j], current_fade_amount[j],  (gint32)(((gint64)samples[i*number_of_channels + j] * current_fade_amount[j]) / (gint64)100));
			}
			samples[i*number_of_channels + j] = (gint32)(((gint64)samples[i*number_of_channels + j] * current_fade_amount[j]) / (gint64)100);

			lastsign[j] = sign[j];

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
