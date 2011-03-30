

#include <math.h>
#include "xmms/xmms_log.h"

#include "xmmspriv/xmms_fade.h"



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
			
			// oh knows this could cause us to fade a different amount on a non zero crossing
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
			
			// oh knows this could cause us to fade a different amount on a non zero crossing
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

			samples[i*number_of_channels + j] = (gint32)(((gint64)samples[i*number_of_channels + j] * current_fade_amount[j]) / (gint64)100);

			lastsign[j] = sign[j];

		}
		
	}

	return 0;
}


// float

int
crossfade_chunk(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	float *samples_from = (float*)sample_buffer_from;
	float *samples_to = (float*)sample_buffer_to;
	float *faded_samples = (float*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_chunk, total_frames;
	number_of_channels = 2;
	
	frames_in_chunk = samples_in_chunk / number_of_channels;
	total_frames = total_samples / number_of_channels;
	

	int i ,j, n;
	int sign[2][number_of_channels], lastsign[2][number_of_channels];
	float next_fade_amount, next_in_fade_amount;
	float current_fade_amount[2][number_of_channels];
	
	for (n = 0; n < 2; n++) {
		for (j = 0; j < number_of_channels; j++) {
			current_fade_amount[n][j] = 0;
			sign[n][j] = 0;
			lastsign[n][j] = -1;
		}
	}
	
	int fuckit;
	fuckit = 0;
	

	for (i = 0; i < frames_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {


				next_in_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels)))  + 0.5)/(float)total_frames);

				next_in_fade_amount = next_in_fade_amount * next_in_fade_amount;
				next_in_fade_amount = 1 - next_in_fade_amount;

				
				if (fuckit == 1) {
					next_in_fade_amount = 100;
				}
							
				
				next_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels))) + 0.5)/(float)total_frames);

				next_fade_amount = next_fade_amount * next_fade_amount;

				
				if (fuckit == 1) {
					next_fade_amount = 0;
				}

			

			if (current_fade_amount[0][j] == 0) {
				current_fade_amount[1][j] = next_in_fade_amount;
				current_fade_amount[0][j] = next_fade_amount;
			}
		
	
		if (samples_from[i*number_of_channels + j] >= 0) {
				sign[0][j] = 1;
			} else {
				sign[0][j] = 0;
			}
		

			if (samples_to[i*number_of_channels + j] >= 0) {
				sign[1][j] = 1;
			} else {
				sign[1][j] = 0;
			}

	for (n = 0; n < 2; n++) {	
			
			if(lastsign[n][j] == -1)
				lastsign[n][j] = sign[n][j];
			
			if (sign[n][j] != lastsign[n][j]) {
			 if(n == 1) {
			 	//XMMS_DBG("fade amount in is: %f ", next_in_fade_amount);
				current_fade_amount[n][j] = next_in_fade_amount;
			} else {
				current_fade_amount[n][j] = next_fade_amount;
			}
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
				if(current_fade_amount[n][j] < 0)
					current_fade_amount[n][j] = 0;
				if(current_fade_amount[n][j] > 100)
					current_fade_amount[n][j] = 100;
			}

		}

			faded_samples[i*number_of_channels + j] = ((((samples_from[sample_start_number + i*number_of_channels + j] * current_fade_amount[0][j]) ) + ((samples_to[i*number_of_channels + j] * current_fade_amount[1][j]) )) );

			lastsign[0][j] = sign[0][j];
			lastsign[1][j] = sign[1][j];

		}
		
	}

	return 0;

}


int
crossfade_chunk_s16(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint16 *samples_from = (gint16*)sample_buffer_from;
	gint16 *samples_to = (gint16*)sample_buffer_to;
	gint16 *faded_samples = (gint16*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_chunk, total_frames;
	number_of_channels = 2;
	
	frames_in_chunk = samples_in_chunk / number_of_channels;
	total_frames = total_samples / number_of_channels;
	

	int i ,j, n;
	int sign[2][number_of_channels], lastsign[2][number_of_channels];
	float next_fade_amount, next_in_fade_amount;
	float current_fade_amount[2][number_of_channels];
	
	for (n = 0; n < 2; n++) {
		for (j = 0; j < number_of_channels; j++) {
			current_fade_amount[n][j] = 0;
			sign[n][j] = 0;
			lastsign[n][j] = -1;
		}
	}
	
	int fuckit;
	fuckit = 0;
	

	for (i = 0; i < frames_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {



				next_in_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels)))  + 0.5)/(float)total_frames);

				next_in_fade_amount = next_in_fade_amount * next_in_fade_amount;
				next_in_fade_amount = 1 - next_in_fade_amount;

				
				if (fuckit == 1) {
					next_in_fade_amount = 100;
				}

							
				
				next_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels))) + 0.5)/(float)total_frames);

				next_fade_amount = next_fade_amount * next_fade_amount;

				
				if (fuckit == 1) {
					next_fade_amount = 0;
				}
			//}
			
			// oh knows this could cause us to fade a different amount on a non zero crossing
			if (current_fade_amount[0][j] == 0) {
				current_fade_amount[1][j] = next_in_fade_amount;
				current_fade_amount[0][j] = next_fade_amount;
			}
		
	
		if (samples_from[i*number_of_channels + j] >= 0) {
				sign[0][j] = 1;
			} else {
				sign[0][j] = 0;
			}
		

			if (samples_to[i*number_of_channels + j] >= 0) {
				sign[1][j] = 1;
			} else {
				sign[1][j] = 0;
			}

	for (n = 0; n < 2; n++) {	
			
			if(lastsign[n][j] == -1)
				lastsign[n][j] = sign[n][j];
			
			if (sign[n][j] != lastsign[n][j]) {
			 if(n == 1) {
			 	//XMMS_DBG("fade amount in is: %f ", next_in_fade_amount);
				current_fade_amount[n][j] = next_in_fade_amount;
			} else {
				current_fade_amount[n][j] = next_fade_amount;
			}
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
				if(current_fade_amount[n][j] < 0)
					current_fade_amount[n][j] = 0;
				if(current_fade_amount[n][j] > 100)
					current_fade_amount[n][j] = 100;
			}

		}

			faded_samples[i*number_of_channels + j] = ((((samples_from[sample_start_number + i*number_of_channels + j] * current_fade_amount[0][j]) ) + ((samples_to[i*number_of_channels + j] * current_fade_amount[1][j]) )) );

			lastsign[0][j] = sign[0][j];
			lastsign[1][j] = sign[1][j];

		}
		
	}

	return 0;

}


int
crossfade_chunk_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_chunk, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint *samples_from = (gint*)sample_buffer_from;
	gint *samples_to = (gint*)sample_buffer_to;
	gint *faded_samples = (gint*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_chunk, total_frames;
	number_of_channels = 2;
	
	frames_in_chunk = samples_in_chunk / number_of_channels;
	total_frames = total_samples / number_of_channels;
	

	int i ,j, n;
	int sign[2][number_of_channels], lastsign[2][number_of_channels];
	float next_fade_amount, next_in_fade_amount;
	float current_fade_amount[2][number_of_channels];
	
	for (n = 0; n < 2; n++) {
		for (j = 0; j < number_of_channels; j++) {
			current_fade_amount[n][j] = 0;
			sign[n][j] = 0;
			lastsign[n][j] = -1;
		}
	}
	
	int fuckit;
	fuckit = 0;
	
	for (i = 0; i < frames_in_chunk; i++) {
		for (j = 0; j < number_of_channels; j++) {



				next_in_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels)))  + 0.5)/(float)total_frames);

				next_in_fade_amount = next_in_fade_amount * next_in_fade_amount;
				next_in_fade_amount = 1 - next_in_fade_amount;

				
				if (fuckit == 1) {
					next_in_fade_amount = 100;
				}

							
				
				next_fade_amount = cos(3.14159*0.5*(((float)(i + (sample_start_number / number_of_channels))) + 0.5)/(float)total_frames);

				next_fade_amount = next_fade_amount * next_fade_amount;

				
				if (fuckit == 1) {
					next_fade_amount = 0;
				}

			
			// oh knows this could cause us to fade a different amount on a non zero crossing
			if (current_fade_amount[0][j] == 0) {
				current_fade_amount[1][j] = next_in_fade_amount;
				current_fade_amount[0][j] = next_fade_amount;
			}
		
	
		if (samples_from[i*number_of_channels + j] >= 0) {
				sign[0][j] = 1;
			} else {
				sign[0][j] = 0;
			}
		

			if (samples_to[i*number_of_channels + j] >= 0) {
				sign[1][j] = 1;
			} else {
				sign[1][j] = 0;
			}

	for (n = 0; n < 2; n++) {	
			
			if(lastsign[n][j] == -1)
				lastsign[n][j] = sign[n][j];
			
			if (sign[n][j] != lastsign[n][j]) {
			 if(n == 1) {
			 	//XMMS_DBG("fade amount in is: %f ", next_in_fade_amount);
				current_fade_amount[n][j] = next_in_fade_amount;
			} else {
				current_fade_amount[n][j] = next_fade_amount;
			}
				//XMMS_DBG ("Zero Crossing at %d", current_sample_number);
				if(current_fade_amount[n][j] < 0)
					current_fade_amount[n][j] = 0;
				if(current_fade_amount[n][j] > 100)
					current_fade_amount[n][j] = 100;
			}

		}

			faded_samples[i*number_of_channels + j] = ((((samples_from[sample_start_number + i*number_of_channels + j] * current_fade_amount[0][j]) ) + ((samples_to[i*number_of_channels + j] * current_fade_amount[1][j]) )) );

			lastsign[0][j] = sign[0][j];
			lastsign[1][j] = sign[1][j];

		}
		
	}

	return 0;

}

