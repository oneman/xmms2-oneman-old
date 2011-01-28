/** @file pitch.c
 *  pitch effect plugin
 *
 *  Copyright (C) 2006-2009 XMMS2 Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "xmms/xmms_xformplugin.h"
#include "xmms/xmms_sample.h"
#include "xmms/xmms_log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>


#include <samplerate.h>



typedef struct {
	gfloat *pvoc;
	SRC_STATE *resampler;

	gint winsize;
	gint channels;
	gint bufsize;

	xmms_sample_t *iobuf;
	xmms_sample_t *procbuf;
	gfloat *resbuf;
	GString *outbuf;

	gfloat speed;
	gfloat pitch;
	SRC_DATA resdata;
	gint attack_detection;
	gboolean enabled;
} xmms_pitch_data_t;

static gboolean xmms_pitch_plugin_setup (xmms_xform_plugin_t *xform_plugin);
static gboolean xmms_pitch_init (xmms_xform_t *xform);
static void xmms_pitch_destroy (xmms_xform_t *xform);
static void xmms_pitch_config_changed (xmms_object_t *object, xmmsv_t *_data, gpointer userdata);
static gint xmms_pitch_read (xmms_xform_t *xform, xmms_sample_t *buf, gint len,
                               xmms_error_t *err);
static gint64 xmms_pitch_seek (xmms_xform_t *xform, gint64 offset,
                                 xmms_xform_seek_mode_t whence,
                                 xmms_error_t *err);

/*
 * Plugin header
 */

XMMS_XFORM_PLUGIN ("pitch",
                   "pitch effect", XMMS_VERSION,
                   "Phase pitch effect plugin",
                   xmms_pitch_plugin_setup);

static gboolean
xmms_pitch_plugin_setup (xmms_xform_plugin_t *xform_plugin)
{
	xmms_xform_methods_t methods;

	XMMS_XFORM_METHODS_INIT (methods);
	methods.init = xmms_pitch_init;
	methods.destroy = xmms_pitch_destroy;
	methods.read = xmms_pitch_read;
	methods.seek = xmms_pitch_seek;

	xmms_xform_plugin_methods_set (xform_plugin, &methods);

	xmms_xform_plugin_config_property_register (xform_plugin, "enabled", "100",
	                                            NULL, NULL);

	xmms_xform_plugin_config_property_register (xform_plugin, "pitch", "100",
	                                            NULL, NULL);

	xmms_xform_plugin_indata_add (xform_plugin,
	                              XMMS_STREAM_TYPE_MIMETYPE,
	                              "audio/pcm",
	                              XMMS_STREAM_TYPE_FMT_FORMAT,
	                              XMMS_SAMPLE_FORMAT_FLOAT,
	                              XMMS_STREAM_TYPE_END);

	return TRUE;
}

static gboolean
xmms_pitch_init (xmms_xform_t *xform)
{
	xmms_pitch_data_t *priv;
	xmms_config_property_t *config;

	g_return_val_if_fail (xform, FALSE);

	priv = g_new0 (xmms_pitch_data_t, 1);
	priv->winsize = 2048;
	priv->channels = xmms_xform_indata_get_int (xform, XMMS_STREAM_TYPE_FMT_CHANNELS);
	priv->bufsize = priv->winsize * priv->channels;

	priv->iobuf = g_malloc (priv->bufsize * sizeof (gfloat));
	priv->procbuf = g_malloc (priv->bufsize * sizeof (gfloat));
	priv->resbuf = g_malloc (priv->bufsize * sizeof (gfloat));
	priv->outbuf = g_string_new (NULL);



	priv->resampler = src_new (SRC_SINC_MEDIUM_QUALITY, priv->channels, NULL);
	g_return_val_if_fail (priv->resampler, FALSE);

	xmms_xform_private_data_set (xform, priv);

	config = xmms_xform_config_lookup (xform, "enabled");
	g_return_val_if_fail (config, FALSE);
	xmms_config_property_callback_set (config, xmms_pitch_config_changed, priv);
	priv->enabled = !!xmms_config_property_get_int (config);

	config = xmms_xform_config_lookup (xform, "pitch");
	g_return_val_if_fail (config, FALSE);
	xmms_config_property_callback_set (config, xmms_pitch_config_changed, priv);
	priv->pitch = 100.0 / (gfloat) xmms_config_property_get_int (config);

	priv->resdata.data_in = NULL;
	priv->resdata.input_frames = 0;
	priv->resdata.data_out = priv->resbuf;
	priv->resdata.output_frames = priv->winsize;
	priv->resdata.src_ratio = priv->pitch;
	priv->resdata.end_of_input = 0;

	xmms_xform_outdata_type_copy (xform);

	return TRUE;
}

static void
xmms_pitch_destroy (xmms_xform_t *xform)
{
	xmms_pitch_data_t *data;
	xmms_config_property_t *config;

	g_return_if_fail (xform);

	data = xmms_xform_private_data_get (xform);
	g_return_if_fail (data);

	config = xmms_xform_config_lookup (xform, "enabled");
	xmms_config_property_callback_remove (config, xmms_pitch_config_changed, data);

	config = xmms_xform_config_lookup (xform, "pitch");
	xmms_config_property_callback_remove (config, xmms_pitch_config_changed, data);

	src_delete (data->resampler);

	g_string_free (data->outbuf, TRUE);
	g_free (data->resbuf);
	g_free (data->procbuf);
	g_free (data->iobuf);
	g_free (data);
}

static void
xmms_pitch_config_changed (xmms_object_t *object, xmmsv_t *_data, gpointer userdata)
{
	xmms_config_property_t *val;
	xmms_pitch_data_t *data;
	const gchar *name;
	gint value;

	g_return_if_fail (object);
	g_return_if_fail (userdata);

	val = (xmms_config_property_t *) object;
	data = (xmms_pitch_data_t *) userdata;

	name = xmms_config_property_get_name (val);
	value = xmms_config_property_get_int (val);

	XMMS_DBG ("config value changed! %s => %d", name, value);
	/* we are passed the full config key, not just the last token,
	 * which makes this code kinda ugly.
	 * fix when bug 97 has been resolved
	 */
	name = strrchr (name, '.') + 1;

	if (!strcmp (name, "enabled")) {
		data->enabled = !!value; }
	else if (!strcmp (name, "pitch") && value != 0) {
		data->pitch = 100.0 / (gfloat) value;
		data->resdata.src_ratio = data->pitch;
	}
}

static gint
xmms_pitch_read (xmms_xform_t *xform, xmms_sample_t *buffer, gint len,
                   xmms_error_t *error)
{
	xmms_pitch_data_t *data;
	guint size;

	g_return_val_if_fail (xform, -1);

	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	size = MIN (data->outbuf->len, len);
	while (size == 0) {
		int i, dpos;
		gfloat *samples = (gfloat *) data->iobuf;

		if (!data->enabled) {
			return xmms_xform_read (xform, buffer, len, error);
		}

		if (!data->resdata.input_frames) {
				int ret, read = 0;


				while (read < data->bufsize * sizeof (gfloat)) {
					ret = xmms_xform_read (xform,
					                       data->iobuf+read,
					                       data->bufsize *
					                       sizeof (gfloat)-read,
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

			data->resdata.data_in = data->iobuf;
			data->resdata.input_frames = 2048;
		}
		src_process (data->resampler, &data->resdata);
		data->resdata.data_in += data->resdata.input_frames_used * data->channels;
		data->resdata.input_frames -= data->resdata.input_frames_used;

	//	for (i=0; i<data->resdata.output_frames_gen * data->channels; i++) {
	//		samples[i] = data->resbuf[i];// * 32767;
	//	}
		g_string_append_len (data->outbuf, data->resbuf,
		                     data->resdata.output_frames_gen *
		                     data->channels *
		                     sizeof (gfloat));
		size = MIN (data->outbuf->len, len);
	}

	memcpy (buffer, data->outbuf->str, size);
	g_string_erase (data->outbuf, 0, size);

	return size;
}

static gint64
xmms_pitch_seek (xmms_xform_t *xform, gint64 offset,
                   xmms_xform_seek_mode_t whence, xmms_error_t *err)
{
	return xmms_xform_seek (xform, offset, whence, err);
}

