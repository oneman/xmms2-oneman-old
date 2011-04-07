
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

	//data = g_new0 (xmms_roll_data_t, 1);
	data->winsize = 512;
	data->channels = 2;
	data->bufsize = data->winsize * data->channels;

	data->iobuf = g_malloc (data->bufsize * sizeof (gfloat));
	data->resbuf = g_malloc (data->bufsize * sizeof (gfloat));
	data->outbuf = g_string_new (NULL);



	data->resampler = src_new (SRC_SINC_MEDIUM_QUALITY, data->channels, NULL);
	g_return_val_if_fail (data->resampler, FALSE);

	
	data->enabled = 1;


	//data->pitch = 100.0 / 88.0; 

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

	//	if (!data->enabled) {
	//		return xmms_xform_read (xform, buffer, len, error);
	//	}

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
					
											XMMS_DBG("Ndfdfdfuck");
					   } else {
					           					ret = xmms_ringbuf_read_cutzero (ringbuf,
					                       data->iobuf+read,
					                       data->bufsize *
					                       sizeof (gfloat)-read
					                       );
					                       
					                       final = 2;            
					      iobufx = data->iobuf;
					      	XMMS_DBG("laaa%f", iobufx[(read / 4) - 4] );            
					      	XMMS_DBG("laaa%f", iobufx[(read / 4) - 3] );            
					      	XMMS_DBG("laaa%f", iobufx[(read / 4) - 2] );            
					       	XMMS_DBG("laaa%f", iobufx[(read / 4) - 1] );            
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
XMMS_DBG ("ggg %d ", data->resdata.input_frames );
		g_string_append_len (data->outbuf, (gchar *)data->resbuf,
		                     data->resdata.output_frames_gen *
		                     data->channels *
		                     sizeof (gfloat));

		size = MIN (data->outbuf->len, len);
	}
			XMMS_DBG ("yarg %d", data->resdata.input_frames );
	memcpy (buffer, data->outbuf->str, size);
	g_string_erase (data->outbuf, 0, size);

	return size;
}
