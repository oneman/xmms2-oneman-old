





/*


roll this and smoke it..

		if (output->fader.current_frame_number == 0) {
			src_reset (output->rolldata->resampler);
		}

		if (output->j == 0.0) { 
			
			if (output->fader.status == FADING_OUT) { 
				output->j = 100.0;
			} else {
				output->j = 85.0;
			}		
			
			output->rolldata->resdata.src_ratio = 100.0 / output->j ;
		}
		
		int x;
		ret = 0;
		int ret2;
		ret2 = 0;
		if(output->fader.current_frame_number <= (output->fader.total_frames - 1524)) {
		while (ret != len )
			ret += xmms_roll_read (output->rolldata, output->reading_ringbuffer, buffer + ret, len - ret, 0);			
		} else {

			output->rolldata->resdata.src_ratio = 1;

			ret2 = xmms_roll_read (output->rolldata, output->reading_ringbuffer, buffer + ret, len - ret, 1);
			XMMS_DBG ("ret2 %d", ret2 );
			ret = ret + ret2;

			x = -1;
			XMMS_DBG ("hel me");
			x = find_final_zero_crossing (buffer, ret);
			if (x != -1) {
				ret = x * 8; 
			}
			

			if (ret < len) {
				XMMS_DBG ("needed %d more", len - ret );
				ret += xmms_ringbuf_read (output->reading_ringbuffer, buffer + ret, len - ret);
			}

		}
			
		if (output->fader.status == FADING_OUT) { 
			output->j = output->j - 0.8;
			output->rolldata->resdata.src_ratio = 100.0 / output->j;
		} else {
			output->j = output->j + 0.15;
			output->rolldata->resdata.src_ratio = 100.0 / output->j;				
		}
		
		
		
*/




/*





	if (output->fader.status) {	
			
		output->fader.format = output->format;
		fade_slice(&output->fader, buffer, ret);
		
		if (output->fader.current_frame_number >= output->fader.total_frames) {
		
			output->j = 0;
		
			if ((output->zero_frames) && (output->fader.status == FADING_OUT)) {
				output->zero_frames_count = output->zero_frames;
			} else {
				fade_complete(output);
			}
		}
	}
	
	
*/



















#include <math.h>
#include "xmms/xmms_log.h"
#include "xmmspriv/xmms_sample.h"
#include "xmmspriv/xmms_streamtype.h"

#include "xmmspriv/xmms_roll.h"
#include <samplerate.h>
xmms_roll_data_t *
xmms_roll_init (xmms_roll_data_t *data)
{
	


	data = g_new0 (xmms_roll_data_t, 1);

	data->winsize = 512;
	data->channels = 2;
	data->bufsize = data->winsize * data->channels;

	data->iobuf = g_malloc (data->bufsize * sizeof (gfloat));
	data->resbuf = g_malloc (data->bufsize * sizeof (gfloat));
	data->outbuf = g_string_new (NULL);



	data->resampler = src_new (SRC_SINC_MEDIUM_QUALITY, data->channels, NULL);
	g_return_val_if_fail (data->resampler, FALSE);

	data->resdata.data_in = NULL;
	data->resdata.input_frames = 0;
	data->resdata.data_out = data->resbuf;
	data->resdata.output_frames = data->winsize;
	data->resdata.src_ratio = 1.0;
	data->resdata.end_of_input = 0;



	return data;
}

 void
xmms_roll_destroy (xmms_roll_data_t *data)
{

	src_delete (data->resampler);

	g_string_free (data->outbuf, TRUE);
	g_free (data->resbuf);
	g_free (data->iobuf);
	g_free (data);
}


 gint
xmms_roll_read (xmms_roll_data_t *data, xmms_ringbuf_t *ringbuf, xmms_sample_t *buffer, gint len, gint final)
{

	guint size;

	int err;



	size = MIN (data->outbuf->len, len);
	while (size == 0) {

		if (!data->resdata.input_frames) {
				int ret, read = 0;

				int y;
				float *iobufx;
				while (read < data->bufsize * sizeof (gfloat)) {
					if (final > 0) {
						if (final == 2) {
		
						iobufx = data->iobuf;
						for (y = 1; read < data->bufsize * sizeof (gfloat); read++) {
							iobufx[read / 4] = 0;
						}
						ret = 0;
						data->resdata.end_of_input = 1;
				
					} else {
				
						ret = xmms_ringbuf_read_cutzero (ringbuf,
					                       data->iobuf+read,
					                       data->bufsize *
					                       sizeof (gfloat)-read
					                       );
					                       
					        final = 2;            
					      	iobufx = data->iobuf;        
					}              
					                       
					                       
				} else {
				
					ret = xmms_ringbuf_read (ringbuf,
					                       data->iobuf+read,
					                       data->bufsize *
					                       sizeof (gfloat)-read
					                       );
					   
				}                    
					                       
				if (ret <= 0) {
					if (!ret && !read) {
							/* end of file */
							return 0;
						} else if (ret < 0) {
							return ret;
						}
						break;
					}
					read += ret;
				}

			data->resdata.data_in = data->iobuf;
			data->resdata.input_frames = data->winsize;
			
	
			
			
		}
		err = src_process (data->resampler, &data->resdata);
		
		if (err != 0) {
			XMMS_DBG ("src err %d", err );
			err = 0;
		}
		
		
		data->resdata.data_in += data->resdata.input_frames_used * data->channels;
		data->resdata.input_frames -= data->resdata.input_frames_used;

		g_string_append_len (data->outbuf, (gchar *)data->resbuf,
		                     data->resdata.output_frames_gen *
		                     data->channels *
		                     sizeof (gfloat));

		size = MIN (data->outbuf->len, len);
	}

	memcpy (buffer, data->outbuf->str, size);
	g_string_erase (data->outbuf, 0, size);

	return size;
}
