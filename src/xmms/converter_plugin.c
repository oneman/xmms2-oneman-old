/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2009 XMMS2 Team
 *
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include <glib.h>

#include "xmmspriv/xmms_xform.h"
#include "xmmspriv/xmms_streamtype.h"
#include "xmmspriv/xmms_sample.h"
#include "xmmspriv/xmms_xform.h"
#include "xmms/xmms_medialib.h"
#include "xmms/xmms_log.h"
#include <string.h>

/* oneman was here */

#include <samplerate.h>

typedef struct xmms_conv_xform_data_St {
	xmms_sample_converter_t *conv;
	/*void *outbuf;*/
	guint outlen;

	int insampletype;
	int outsampletype;

	SRC_STATE *resampler;

	gint winsize;
	gint channels;
	gint bufsize;

	xmms_sample_t *iobuf;
	xmms_sample_t *iobuf2;
	xmms_sample_t *iobuf3;
	gfloat *resbuf;
	GString *outbuf;

	/*gfloat pitch;*/
	SRC_DATA resdata;

} xmms_conv_xform_data_t;

static xmms_xform_plugin_t *converter_plugin;

static gboolean
xmms_converter_plugin_init (xmms_xform_t *xform)
{
	xmms_conv_xform_data_t *priv;
	xmms_stream_type_t *intype;
	xmms_stream_type_t *to;
	const GList *goal_hints;

	intype = xmms_xform_intype_get (xform);
	goal_hints = xmms_xform_goal_hints_get (xform);

	to = xmms_stream_type_coerce (intype, goal_hints);
	if (!to) {
		return FALSE;
	}

	gint fformat, fsamplerate, fchannels;
	gint tformat, tsamplerate, tchannels;

	fformat = xmms_stream_type_get_int (intype, XMMS_STREAM_TYPE_FMT_FORMAT);
	fsamplerate = xmms_stream_type_get_int (intype, XMMS_STREAM_TYPE_FMT_SAMPLERATE);
	fchannels = xmms_stream_type_get_int (intype, XMMS_STREAM_TYPE_FMT_CHANNELS);
	tformat = xmms_stream_type_get_int (to, XMMS_STREAM_TYPE_FMT_FORMAT);
	tsamplerate = xmms_stream_type_get_int (to, XMMS_STREAM_TYPE_FMT_SAMPLERATE);
	tchannels = xmms_stream_type_get_int (to, XMMS_STREAM_TYPE_FMT_CHANNELS);


			XMMS_DBG ("onemans secret rabbit converter FROM: Sample Format: %s. Samplerate: %d. Channels: %d.",
			          xmms_sample_name_get(fformat),
			          fsamplerate,
			          fchannels);

			XMMS_DBG ("To: Sample Format: %s. Samplerate: %d. Channels: %d.",
			          xmms_sample_name_get(tformat),
			          tsamplerate,
			          tchannels);

	XMMS_DBG ("Hardcoded to two channels currently sry");

	XMMS_DBG ("Can Take in S16/S32/FLOAT and Output S16/S32/FLOAT and handle any sample rate, I think...");

	xmms_xform_outdata_type_set (xform, to);
	xmms_object_unref (to);

	priv = g_new0 (xmms_conv_xform_data_t, 1);
	priv->winsize = 2048;
	priv->channels = 2; //xmms_xform_indata_get_int (xform, XMMS_STREAM_TYPE_FMT_CHANNELS);
	priv->bufsize = priv->winsize * priv->channels;

	priv->insampletype = fformat;
	priv->outsampletype = tformat;

	int readbytesize;
	int writebytesize;
	if(priv->insampletype == 3) {
		readbytesize = 2; 
	} else {
		readbytesize = 4; 
	}

	if(priv->outsampletype == 3) {
		writebytesize = 2; 
	} else {
		writebytesize = 4; 
	}


	priv->iobuf = g_malloc (priv->bufsize * readbytesize);
	priv->iobuf2 = g_malloc (priv->bufsize * sizeof (gfloat));
	priv->iobuf3 = g_malloc (priv->bufsize * writebytesize);
	priv->resbuf = g_malloc (priv->bufsize * sizeof (gfloat));
	priv->outbuf = g_string_new (NULL);

	priv->resampler = src_new (SRC_SINC_BEST_QUALITY, priv->channels, NULL);
	g_return_val_if_fail (priv->resampler, FALSE);


	priv->resdata.data_in = NULL;
	priv->resdata.input_frames = 0;
	priv->resdata.data_out = priv->resbuf;
	priv->resdata.output_frames = priv->winsize;
	priv->resdata.src_ratio = (1.0 * tsamplerate) / fsamplerate;
	priv->resdata.end_of_input = 0;


	xmms_xform_private_data_set (xform, priv);

	return TRUE;
}

static void
xmms_converter_plugin_destroy (xmms_xform_t *xform)
{
	xmms_conv_xform_data_t *data;

	data = xmms_xform_private_data_get (xform);

	if (data) {

	src_delete (data->resampler);

	g_string_free (data->outbuf, TRUE);
	g_free (data->resbuf);
	g_free (data->iobuf);
	g_free (data->iobuf2);
	g_free (data->iobuf3);
	g_free (data);

	}
}

static gint
xmms_converter_plugin_read (xmms_xform_t *xform, void *buffer, gint len, xmms_error_t *error)
{
	xmms_conv_xform_data_t *data;

	guint size;

	g_return_val_if_fail (xform, -1);

	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	int readbytesize;
	int writebytesize;

	if(data->insampletype == 3) {
		readbytesize = 2; 
	} else {
		readbytesize = 4; 
	}


	size = MIN (data->outbuf->len, len);
	while (size == 0) {

		if (!data->resdata.input_frames) {
				int ret, read = 0;


				while (read < data->bufsize * readbytesize) {
					ret = xmms_xform_read (xform,
					                       data->iobuf+read,
					                       data->bufsize *
					                       readbytesize-read,
					                       error);
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

		if(data->insampletype == 3) {
			src_short_to_float_array (data->iobuf, data->iobuf2, data->bufsize) ;
		} else {
			src_int_to_float_array (data->iobuf, data->iobuf2, data->bufsize) ;
		}

			data->resdata.data_in = data->iobuf2;
			data->resdata.input_frames = data->winsize;
		}
		src_process (data->resampler, &data->resdata);
		data->resdata.data_in += data->resdata.input_frames_used * data->channels;
		data->resdata.input_frames -= data->resdata.input_frames_used;

		g_string_append_len (data->outbuf, (gchar *)data->resbuf,
		                     data->resdata.output_frames_gen *
		                     data->channels *
		                     sizeof (gfloat));

		size = MIN (data->outbuf->len, len);
	}


	if(data->outsampletype == 3) {
		writebytesize = 2; 
		src_float_to_short_array ((gfloat *)data->outbuf->str, data->iobuf3, size);
		memcpy (buffer, data->iobuf3, size);
	} 

	if(data->outsampletype == 5) {
		writebytesize = 4; 
		src_float_to_int_array ((gfloat *)data->outbuf->str, data->iobuf3, size);
		memcpy (buffer, data->iobuf3, size);
	} 

	if(data->outsampletype == 7) {
		writebytesize = 4; 
		memcpy (buffer, data->outbuf->str, size);
	} 

	g_string_erase (data->outbuf, 0, size);

	return size;


}

static gint64
xmms_converter_plugin_seek (xmms_xform_t *xform, gint64 samples, xmms_xform_seek_mode_t whence, xmms_error_t *err)
{
	xmms_conv_xform_data_t *data;
	gint64 res;
	gint64 scaled_samples;

	g_return_val_if_fail (whence == XMMS_XFORM_SEEK_SET, -1);
	g_return_val_if_fail (xform, -1);

	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	scaled_samples = samples / data->resdata.src_ratio;

	res = xmms_xform_seek (xform, scaled_samples, XMMS_XFORM_SEEK_SET, err);
	if (res == -1) {
		return -1;
	}

	scaled_samples =  res * data->resdata.src_ratio;

	src_reset(data->resampler);


	return scaled_samples;
}

static gboolean
xmms_converter_plugin_setup (xmms_xform_plugin_t *xform_plugin)
{
	xmms_xform_methods_t methods;

	XMMS_XFORM_METHODS_INIT (methods);
	methods.init = xmms_converter_plugin_init;
	methods.destroy = xmms_converter_plugin_destroy;
	methods.read = xmms_converter_plugin_read;
	methods.seek = xmms_converter_plugin_seek;

	xmms_xform_plugin_methods_set (xform_plugin, &methods);

	/*
	 * Handle any pcm data...
	 * Well, we don't really.. (now we almost kinda do)
	 */
	xmms_xform_plugin_indata_add (xform_plugin,
	                              XMMS_STREAM_TYPE_MIMETYPE,
	                              "audio/pcm",
	                              XMMS_STREAM_TYPE_PRIORITY,
				      100,
	                              XMMS_STREAM_TYPE_NAME,
				      "generic-pcmdata",
	                              XMMS_STREAM_TYPE_END);

	converter_plugin = xform_plugin;
	return TRUE;
}

/*
xmms_xform_t *
xmms_converter_new (xmms_xform_t *prev, xmms_medialib_entry_t entry, GList *gt)
{
	xmms_xform_t *xform;

	xform = xmms_xform_new (converter_plugin, prev, entry, gt);

	return xform;
}
*/

XMMS_XFORM_BUILTIN (converter,
                    "Sample format converter",
                    XMMS_VERSION,
                    "Sample format converter",
                    xmms_converter_plugin_setup);
