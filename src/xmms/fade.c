

#include <math.h>
#include "xmms/xmms_log.h"
#include "xmmspriv/xmms_sample.h"
#include "xmmspriv/xmms_streamtype.h"
#include "xmmspriv/xmms_fade.h"


int find_final_zero_crossing (void *buffer, int len) {
						XMMS_DBG("hi crosa %d", len);
	float *samples_float;
	samples_float = (float*)buffer;
	int frames, channels;
		//frames = len / xmms_sample_frame_size_get(fader->format);
			frames = len / 8;
			channels = 2;
			int final_frame[2];
						int sign[2];
			int lastsign[2];
			
			int i , j;
final_frame[0] = -5;
final_frame[1] = -666;

				for (j = 0; j < channels; j++) {
					if (samples_float[(frames - 1)*channels + j] >= 0) {
						lastsign[j] = 1;
					} else {
						lastsign[j] = 0;
					}
				}
				
				
			for (i = frames - 1; i > -1; i--) {	
				for (j = 0; j < channels; j++) {

						if (samples_float[i*channels + j] >= 0) {
							sign[j] = 1;
						} else {
							sign[j] = 0;
						}
			
						if (sign[j] != lastsign[j]) {	
						
						
						   if ((samples_float[i*channels + j] != 0.0) && (samples_float[i*channels + j] != -0.0)) {
							final_frame[j] = i + 1;
							
							}
							
						}
						
						lastsign[j] = sign[j];
						
					}
					
						if (final_frame[0] == final_frame[1]) {	
						
						XMMS_DBG("final 1 %f", samples_float[(final_frame[0] * 2) - 4]);
						XMMS_DBG("final + %f", samples_float[(final_frame[1] * 2) - 3 ]);	
						
					XMMS_DBG("final 1 %f", samples_float[(final_frame[0] * 2) - 1]);
						XMMS_DBG("final + %f", samples_float[(final_frame[1] * 2) - 2 ]);

									XMMS_DBG("final ::1 %f", samples_float[(final_frame[0] * 2) ]);
						XMMS_DBG("final ::+ %f", samples_float[(final_frame[1] * 2) + 1]);	

							
							XMMS_DBG("final 1 %f", samples_float[(final_frame[0] * 2) + 1]);
						XMMS_DBG("final + %f", samples_float[(final_frame[1] * 2) + 2 ]);
						

						
						XMMS_DBG("final 1 %f", samples_float[(final_frame[0] * 2) + 3]);
						XMMS_DBG("final + %f", samples_float[(final_frame[1] * 2) + 4 ]);
			
					XMMS_DBG("final 1 %f", samples_float[(final_frame[0] * 2) + 5]);
						XMMS_DBG("final + %f", samples_float[(final_frame[1] * 2) + 6 ]);
			
						
						
							XMMS_DBG("found stereo zero corssing yah! %d", final_frame[0]);
							return final_frame[0];
						}
					
				}
				

						XMMS_DBG("No Zero stereo Cross found fuck");
									return -1;
					
				
		
	

}



void
fade_slice(xmms_fader_t *fader, void *buffer, int len) {

	int frames, channels, i, j, extra_frames;
	gint16 *samples_s16;
	gint32 *samples_s32;
	float *samples_float;
	
	frames = len / xmms_sample_frame_size_get(fader->format);

	/* Fading in extra frames are ignored, Fading out they are zero'ed */
	
	if ((frames + fader->current_frame_number) >= fader->total_frames) {
		extra_frames = frames - (fader->total_frames - fader->current_frame_number);
		frames = fader->total_frames - fader->current_frame_number;
	}

	samples_s16 = (gint16*)buffer;
	samples_s32 = (gint32*)buffer;
	samples_float = (float*)buffer;

	channels = xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_CHANNELS);

	int sign[channels];
	int finalsign[channels];
	
	/* First Run Setup */
	if (fader->current_frame_number == 0) {
		for (j = 0; j < channels; j++) {
			if (fader->status == FADING_OUT) {
				fader->current_fade_amount[j] = 1.0;
			} else {
				fader->current_fade_amount[j] = 0.0;
			}
			fader->final_frame[j] = fader->total_frames;
		}
	}
	
		
	/*	The following is repeated in the way that it is for maximum performance, as it could 
	 *	be called half a million times per second
	 */


	if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S16)
	{
	
		samples_s16 = (gint16*)buffer;

		/* First Zero Finder */
		if (fader->current_frame_number == 0) {
			for (j = 0; j < channels; j++) {
				if (samples_s16[0*channels + j] >= 0) {
					fader->lastsign[j] = 1;
				} else {
					fader->lastsign[j] = 0;
				}
			}
		}
		
		
		/* Final Zero Finder */
		if ((frames + fader->current_frame_number) >= fader->total_frames) {
			for (j = 0; j < channels; j++) {
				if (samples_s16[(frames - 1)*channels + j] >= 0) {
					finalsign[j] = 1;
				} else {
					finalsign[j] = 0;
				}
			}
			
			for (j = 0; j < channels; j++) {
				for (i = frames - 1; i > -1; i--) {	
					if (samples_s16[i*channels + j] >= 0) {
						sign[j] = 1;
					} else {
						sign[j] = 0;
					}
			
					if (sign[j] != finalsign[j]) {
						fader->final_frame[j] = fader->current_frame_number + i + 1;
						break;
					}
				}
				if (fader->final_frame[j] == fader->total_frames) {
					XMMS_DBG("No Zero Cross in channel %d final slice.. a tiny artifact may be heard..", j);
				}
			}
		
			if (fader->status == FADING_OUT) {
		
				for (j = 0; j < channels; j++) {
					for (i = (frames - (fader->total_frames - fader->final_frame[j]) - 1); i < frames + extra_frames; i++) {
						samples_s16[i*channels + j] = 0;
					}
				}
			}
		}
		

		for (i = 0; i < frames; i++) {
			for (j = 0; j < channels; j++) {
				
				if ((i + fader->current_frame_number + 1) <= fader->final_frame[j]) {
				
					if (samples_s16[i*channels + j] >= 0) {
						sign[j] = 1;
					} else {
						sign[j] = 0;
					}
			
					if (sign[j] != fader->lastsign[j]) {
				
						if (fader->status == FADING_OUT) {	
							fader->current_fade_amount[j] = ((fader->total_frames - (fader->current_frame_number + i)) * 100.0) / fader->total_frames;
						} else {
							fader->current_fade_amount[j] = ((fader->current_frame_number + i) * 100.0) / fader->total_frames;
						}
						fader->current_fade_amount[j] /= 100.0;
						fader->current_fade_amount[j] *= fader->current_fade_amount[j];
					}

					samples_s16[i*channels + j] = samples_s16[i*channels + j] * fader->current_fade_amount[j];
					fader->lastsign[j] = sign[j];
			
				}
			}
		
		}
	
	} else {
	
		if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S32)
		{
		
			samples_s32 = (gint32*)buffer;
			
			/* First Zero Finder */
			if (fader->current_frame_number == 0) {
				for (j = 0; j < channels; j++) {
					if (samples_s32[0*channels + j] >= 0) {
						fader->lastsign[j] = 1;
					} else {
						fader->lastsign[j] = 0;
					}
				}
			}
		
		
			/* Final Zero Finder */
			if ((frames + fader->current_frame_number) >= fader->total_frames) {
				for (j = 0; j < channels; j++) {
					if (samples_s32[(frames - 1)*channels + j] >= 0) {
						finalsign[j] = 1;
					} else {
						finalsign[j] = 0;
					}
				}
			
				for (j = 0; j < channels; j++) {
					for (i = frames - 1; i > -1; i--) {	
						if (samples_s32[i*channels + j] >= 0) {
							sign[j] = 1;
						} else {
							sign[j] = 0;
						}
			
						if (sign[j] != finalsign[j]) {
							fader->final_frame[j] = fader->current_frame_number + i + 1;
							break;
						}
					}
					if (fader->final_frame[j] == fader->total_frames) {
						XMMS_DBG("No Zero Cross in channel %d final slice.. a tiny artifact may be heard..", j);
					}
				}
		
					if (fader->status == FADING_OUT) {
			
					for (j = 0; j < channels; j++) {
						for (i = (frames - (fader->total_frames - fader->final_frame[j]) - 1); i < frames + extra_frames; i++) {
							samples_s32[i*channels + j] = 0;
						}
					}
				}
			}

			for (i = 0; i < frames; i++) {
				for (j = 0; j < channels; j++) {
	
					if ((i + fader->current_frame_number + 1) <= fader->final_frame[j]) {
	
						if (samples_s32[i*channels + j] >= 0) {
							sign[j] = 1;
						} else {
							sign[j] = 0;
						}
			
						if (sign[j] != fader->lastsign[j]) {
							if (fader->status == FADING_OUT) {	
								fader->current_fade_amount[j] = ((fader->total_frames - (fader->current_frame_number + i)) * 100.0) / fader->total_frames;
							} else {
								fader->current_fade_amount[j] = ((fader->current_frame_number + i) * 100.0) / fader->total_frames;
							}
							fader->current_fade_amount[j] /= 100.0;
							fader->current_fade_amount[j] *= fader->current_fade_amount[j];
						}
	
						samples_s32[i*channels + j] = samples_s32[i*channels + j] * fader->current_fade_amount[j];
						fader->lastsign[j] = sign[j];

					}
				
				}
		
			}
	
	
		} else if (xmms_stream_type_get_int(fader->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_FLOAT) {
		
			samples_float = (float*)buffer;
			
			/* First Zero Finder */
			if (fader->current_frame_number == 0) {
				for (j = 0; j < channels; j++) {
					if (samples_float[0*channels + j] >= 0) {
						fader->lastsign[j] = 1;
					} else {
						fader->lastsign[j] = 0;
					}
				}
			}
		
		
			/* Final Zero Finder */
			if ((frames + fader->current_frame_number) >= fader->total_frames) {
				for (j = 0; j < channels; j++) {
					if (samples_float[(frames - 1)*channels + j] >= 0) {
						finalsign[j] = 1;
					} else {
						finalsign[j] = 0;
					}
				}
				
				for (j = 0; j < channels; j++) {
					for (i = frames - 1; i > -1; i--) {	
						if (samples_float[i*channels + j] >= 0) {
							sign[j] = 1;
						} else {
							sign[j] = 0;
						}
			
						if (sign[j] != finalsign[j]) {	
							fader->final_frame[j] = fader->current_frame_number + i + 1;
							break;
						}
					}
					if (fader->final_frame[j] == fader->total_frames) {
						XMMS_DBG("No Zero Cross in channel %d final slice.. a tiny artifact may be heard..", j);
					}
				}
		
				if (fader->status == FADING_OUT) {
			
					for (j = 0; j < channels; j++) {
						for (i = (frames - (fader->total_frames - fader->final_frame[j]) - 1); i < frames + extra_frames; i++) {
							samples_float[i*channels + j] = 0;
						}
					}
				}
			}

			for (i = 0; i < frames; i++) {
				for (j = 0; j < channels; j++) {
				
					if ((i + fader->current_frame_number + 1) <= fader->final_frame[j]) {
	
						if (samples_float[i*channels + j] >= 0) {	
							sign[j] = 1;
						} else {
							sign[j] = 0;
						}
			
						if (sign[j] != fader->lastsign[j]) {
							if (fader->status == FADING_OUT) {	
								fader->current_fade_amount[j] = ((fader->total_frames - (fader->current_frame_number + i)) * 100.0) / fader->total_frames;
							} else {
								fader->current_fade_amount[j] = ((fader->current_frame_number + i) * 100.0) / fader->total_frames;
							}
							fader->current_fade_amount[j] /= 100.0;
							fader->current_fade_amount[j] *= fader->current_fade_amount[j];
						}

						samples_float[i*channels + j] = samples_float[i*channels + j] * fader->current_fade_amount[j];
						fader->lastsign[j] = sign[j];

					}
				}
			}
			
		}
	}
	
	fader->current_frame_number += frames;

}



// Crossfade...

int
crossfade_slice_float(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_slice, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	float *samples_from = (float*)sample_buffer_from;
	float *samples_to = (float*)sample_buffer_to;
	float *faded_samples = (float*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_slice, total_frames;
	number_of_channels = 2;
	
	frames_in_slice = samples_in_slice / number_of_channels;
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
	

	for (i = 0; i < frames_in_slice; i++) {
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
crossfade_slice_s16(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_slice, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint16 *samples_from = (gint16*)sample_buffer_from;
	gint16 *samples_to = (gint16*)sample_buffer_to;
	gint16 *faded_samples = (gint16*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_slice, total_frames;
	number_of_channels = 2;
	
	frames_in_slice = samples_in_slice / number_of_channels;
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
	

	for (i = 0; i < frames_in_slice; i++) {
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
crossfade_slice_s32(void *sample_buffer_from, void *sample_buffer_to, void *faded_sample_buffer, int sample_start_number, int samples_in_slice, int total_samples) {

	/* ok lets handle those void * and hard code it to float for now, is it possible to cast without a new var?? */
	
	gint *samples_from = (gint*)sample_buffer_from;
	gint *samples_to = (gint*)sample_buffer_to;
	gint *faded_samples = (gint*)faded_sample_buffer;

	/* well as with the sample type we dont really want to hard code the number of channels */
	int number_of_channels, frames_in_slice, total_frames;
	number_of_channels = 2;
	
	frames_in_slice = samples_in_slice / number_of_channels;
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
	
	for (i = 0; i < frames_in_slice; i++) {
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

