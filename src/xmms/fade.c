

#include <math.h>
#include "xmms/xmms_log.h"
#include "xmmspriv/xmms_sample.h"
#include "xmmspriv/xmms_streamtype.h"
#include "xmmspriv/xmms_fade.h"

float next_fade_amount(xmms_fader_t *fader, int i ) {

	float next_fade_amount;

	if (fader->status == FADING_IN) {	
		next_fade_amount = ((float)(i + (fader->current_frame_number))* 100.0)/(float)fader->total_frames;
	} else {
		next_fade_amount = ((float)(fader->total_frames - (i + (fader->current_frame_number)))* 100.0)/(float)fader->total_frames;
	}
	
	if ((next_fade_amount < 0) || (next_fade_amount > 100)) {
		if (next_fade_amount < 0) 
			next_fade_amount = 0;
		if (next_fade_amount > 100) 
			next_fade_amount = 100;
	}
	
	next_fade_amount = next_fade_amount/10.0;
	next_fade_amount = next_fade_amount * next_fade_amount;
	
	

	return next_fade_amount;

}


void
fade_slice(xmms_fader_t *fader, void *buffer, int len) {

	int frames, channels, i, j;
	gint16 *samples_s16;
	gint32 *samples_s32;
	float *samples_float;
	
	frames = len / xmms_sample_frame_size_get(fader->format);

	samples_s16 = (gint16*)buffer;
	samples_s32 = (gint32*)buffer;
	samples_float = (float*)buffer;

	channels = xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT);

	int sign[channels];
	
	if (fader->current_frame_number == 0) {
		for (j = 0; j < channels; j++) {
			fader->current_fade_amount[j] = next_fade_amount(fader, 0);;
			sign[j] = 0;
			fader->lastsign[j] = 0;
		}
	}
	
	/*	The following is repeated in the way that it is for maximum performance, as it could 
	 *	be called half a million times per second
	 */


	if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S16)
	{
	
		samples_s16 = (gint16*)buffer;

		for (i = 0; i < frames; i++) {
			for (j = 0; j < channels; j++) {
	
				if (samples_s16[i*channels + j] >= 0) {
					sign[j] = 1;
				} else {
					sign[j] = 0;
				}
			
				if (sign[j] != fader->lastsign[j]) {
					fader->current_fade_amount[j] = next_fade_amount(fader, i);
				}

				samples_s16[i*channels + j] = ((samples_s16[i*channels + j] * fader->current_fade_amount[j]) / 100);
				fader->lastsign[j] = sign[j];

			}
		
		}
	
	} else {
	
		if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S32)
		{
		
			samples_s32 = (gint32*)buffer;

			for (i = 0; i < frames; i++) {
				for (j = 0; j < channels; j++) {
	
					if (samples_s32[i*channels + j] >= 0) {
						sign[j] = 1;
					} else {
						sign[j] = 0;
					}
			
					if (sign[j] != fader->lastsign[j]) {
						fader->current_fade_amount[j] = next_fade_amount(fader, i);
					}

					samples_s32[i*channels + j] = ((samples_s32[i*channels + j] * fader->current_fade_amount[j]) / 100);
					fader->lastsign[j] = sign[j];

				}
		
			}
	
	
		} else if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_FLOAT) {
		
			samples_float = (float*)buffer;

			for (i = 0; i < frames; i++) {
				for (j = 0; j < channels; j++) {
	
					if (samples_float[i*channels + j] >= 0) {
						sign[j] = 1;
					} else {
						sign[j] = 0;
					}
			
					if (sign[j] != fader->lastsign[j]) {
						fader->current_fade_amount[j] = next_fade_amount(fader, i);
					}

					samples_float[i*channels + j] = ((samples_float[i*channels + j] * fader->current_fade_amount[j]) / 100);
					fader->lastsign[j] = sign[j];

				}
		
			}
			
		}
	}
	
	fader->current_frame_number += frames;

}



// Crossfade...

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

