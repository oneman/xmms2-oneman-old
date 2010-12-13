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

#include "xmms/xmms_outputplugin.h"
#include "xmms/xmms_log.h"

#include <glib.h>
#include <jack/jack.h>
#include <math.h>

/*
 *  Defines
 */

/* this isn't really what we want... */
#define CHANNELS 2


/*
 * Type definitions
 */

typedef struct xmms_jack_data_St {
	jack_client_t *jack;
	jack_port_t *ports[CHANNELS];
	/*           ports */
	gint chunksiz;
	gboolean error;
	gboolean running;
	gint status;
	gint fade_samples;
	gint faded_samples;
	gint fading;
	gint fade_db_range;
	gint connect_to_phys_on_startup;
} xmms_jack_data_t;


/*
 * Function prototypes
 */

float db_to_value(float db);
static gboolean xmms_jack_plugin_setup (xmms_output_plugin_t *plugin);
static gboolean xmms_jack_new (xmms_output_t *output);
static void xmms_jack_destroy (xmms_output_t *output);
static gboolean xmms_jack_status (xmms_output_t *output, xmms_playback_status_t status);
static void xmms_jack_flush (xmms_output_t *output);

static int xmms_jack_process (jack_nframes_t frames, void *arg);
static void xmms_jack_shutdown (void *arg);
static void xmms_jack_error (const gchar *desc);

static gboolean xmms_jack_ports_connected (xmms_jack_data_t *data);
static gboolean xmms_jack_connect_ports (xmms_jack_data_t *data);


/*
 * Plugin header
 */

XMMS_OUTPUT_PLUGIN ("jack", "Jack Output", XMMS_VERSION,
                    "Jack audio server output plugin",
                    xmms_jack_plugin_setup);

static gboolean
xmms_jack_plugin_setup (xmms_output_plugin_t *plugin)
{
	xmms_output_methods_t methods;

	XMMS_OUTPUT_METHODS_INIT (methods);

	methods.new = xmms_jack_new;
	methods.destroy = xmms_jack_destroy;

	methods.status = xmms_jack_status;
	methods.flush = xmms_jack_flush;

	xmms_output_plugin_methods_set (plugin, &methods);

	xmms_output_plugin_config_property_register (plugin, "clientname", "XMMS2",
	                                             NULL, NULL);

	xmms_output_plugin_config_property_register (plugin, "fading_time_ms", "100",
	                                             NULL, NULL);

	xmms_output_plugin_config_property_register (plugin, "fade_db_range", "90",
	                                             NULL, NULL);

	xmms_output_plugin_config_property_register (plugin, "connect_to_phys_on_startup", "1",
	                                             NULL, NULL);

	jack_set_error_function (xmms_jack_error);

	return TRUE;
}


/*
 * Member functions
 */


static gboolean
xmms_jack_connect (xmms_output_t *output, xmms_jack_data_t *data)
{
	int i;

	const xmms_config_property_t *cv;
	const gchar *clientname;

	cv = xmms_output_config_lookup (output, "clientname");
	clientname = xmms_config_property_get_string (cv);

	data->jack = jack_client_open (clientname, JackNullOption, NULL);
	if (!data->jack) {
		return FALSE;
	}

	jack_set_process_callback (data->jack, xmms_jack_process, output);
	jack_on_shutdown (data->jack, xmms_jack_shutdown, output);


	for (i = 0; i < CHANNELS; i++) {
		gchar name[16];
		g_snprintf (name, sizeof (name), "out_%d", i + 1);
		data->ports[i] = jack_port_register (data->jack, name,
		                                     JACK_DEFAULT_AUDIO_TYPE,
		                                     (JackPortIsOutput |
		                                      JackPortIsTerminal), 0);
	}

	data->chunksiz = jack_get_buffer_size (data->jack);

	if (jack_activate (data->jack)) {
		/* jadda jadda */
		return FALSE;
	}

	data->error = FALSE;

	return TRUE;
}


static gboolean
xmms_jack_new (xmms_output_t *output)
{
	xmms_jack_data_t *data;
	const xmms_config_property_t *cv;
	int fading_time_ms;

	g_return_val_if_fail (output, FALSE);
	data = g_new0 (xmms_jack_data_t, 1);

	cv = xmms_output_config_lookup (output, "fading_time_ms");
	fading_time_ms = xmms_config_property_get_int (cv);

	cv = xmms_output_config_lookup (output, "fade_db_range");
	data->fade_db_range = xmms_config_property_get_int (cv);

	cv = xmms_output_config_lookup (output, "connect_to_phys_on_startup");
	data->connect_to_phys_on_startup = xmms_config_property_get_int (cv);

	if (data->fade_db_range > 144) data->fade_db_range = 144;

	data->faded_samples = 0;
	data->fading = 2;
	data->running = FALSE;

	xmms_output_private_data_set (output, data);

	if (!xmms_jack_connect (output, data)) {
		return FALSE;
	}

	if(data->connect_to_phys_on_startup) {

		if (!xmms_jack_ports_connected (data) && !xmms_jack_connect_ports (data)) {
			return FALSE;
		}	
	}

	data->fade_samples = ((fading_time_ms * ((float)jack_get_sample_rate (data->jack) / 1000.0)) - 100);
	XMMS_DBG ("Fade Samples is %d", data->fade_samples);

	xmms_output_format_add (output, XMMS_SAMPLE_FORMAT_FLOAT, CHANNELS,
	                        jack_get_sample_rate (data->jack));

	/* we should connect the ports here? */
	
	xmms_log_info ("Started Patched xmms2 jack output");

	return TRUE;
}


static void
xmms_jack_destroy (xmms_output_t *output)
{
	xmms_jack_data_t *data;

	g_return_if_fail (output);

	data = xmms_output_private_data_get (output);
	g_return_if_fail (data);

	if (data->jack) {
		jack_deactivate (data->jack);
		jack_client_close (data->jack);
	}

	g_free (data);
}


static gboolean
xmms_jack_ports_connected (xmms_jack_data_t *data)
{
	gint is_connected = 0;
	gint i;

	for (i = 0; i < CHANNELS; i++) {
		is_connected += jack_port_connected (data->ports[i]);
	}

	return (is_connected > 0);
}

static gboolean
xmms_jack_connect_ports (xmms_jack_data_t *data)
{
	const gchar **remote_ports;
	gboolean ret = TRUE;
	gint i, err;

	remote_ports = jack_get_ports (data->jack, NULL, NULL,
	                               JackPortIsInput | JackPortIsPhysical);

	for (i = 0; i < CHANNELS && remote_ports && remote_ports[i]; i++) {
		const gchar *src_port = jack_port_name (data->ports[i]);

		err = jack_connect (data->jack, src_port, remote_ports[i]);
		if (err < 0) {
			ret = FALSE;
			break;
		}
	}

	return ret;
}

static gboolean
xmms_jack_status (xmms_output_t *output, xmms_playback_status_t status)
{
	xmms_jack_data_t *data;

	g_return_val_if_fail (output, FALSE);
	data = xmms_output_private_data_get (output);
	g_return_val_if_fail (data, FALSE);

	if (data->error && !xmms_jack_connect (output, data)) {
		return FALSE;
	}

	XMMS_DBG ("I got a status: %d", status);
	data->status = status;

	if (status == 666) {
		XMMS_DBG ("I got Seeked!");
		return TRUE;
	}

	if (status == XMMS_PLAYBACK_STATUS_PLAY) {
		data->running = TRUE;
	} else {
		data->running = FALSE;
	}

	return TRUE;
}


static void
xmms_jack_flush (xmms_output_t *output)
{
	/* do nothing... */
}

float db_to_value(float db)
{
  /* 20 log rule */
  return powf(10.0, db/20.0);
}

static int
xmms_jack_process (jack_nframes_t frames, void *arg)
{
	xmms_output_t *output = (xmms_output_t*) arg;
	xmms_jack_data_t *data;
	xmms_samplefloat_t *buf[CHANNELS];
	xmms_samplefloat_t tbuf[CHANNELS*1024];
	gint i, j, res, toread;

	g_return_val_if_fail (output, -1);
	data = xmms_output_private_data_get (output);
	g_return_val_if_fail (data, -1);

	for (i = 0; i < CHANNELS; i++) {
		buf[i] = jack_port_get_buffer (data->ports[i], frames);
	}

	xmms_samplefloat_t fade_per_sample = ((data->fade_db_range - 10) * -1.0f) / (xmms_samplefloat_t)data->fade_samples;
	xmms_samplefloat_t sample;
	xmms_samplefloat_t sample_result;
	xmms_samplefloat_t fade_value;

	toread = frames;

	if((data->status != 0) && (data->running == FALSE) && ((data->faded_samples <= data->fade_samples) && (data->fading == 0))){
			data->fading = 1;
		XMMS_DBG ("Fade Begin");

	}

	if((data->running == FALSE) && ((data->faded_samples >= data->fade_samples) && (data->fading == 1))){
			data->fading = 2;
		XMMS_DBG ("Fade Complete");

	}

	if (data->status == 0) {
			data->fading = 3;
	}

	if((data->running == TRUE) && (data->fading == 2)){
			data->fading = 0;
			data->faded_samples = 0;
		XMMS_DBG ("Fade Reset");

	}

	if((data->running == FALSE) && (data->fading == 2)){
			data->fading = 3;
			data->faded_samples = 0;
		  XMMS_DBG ("Fade In Preped");

	}

	if((data->running == TRUE) && (data->fading == 3)){
			data->fading = 4;
		  XMMS_DBG ("Fade In Begin");

	}

	if((data->running == TRUE) && ((data->faded_samples >= data->fade_samples) && (data->fading == 4))){
			data->fading = 2;
		XMMS_DBG ("Fade In Complete");

	}


	if ((data->running) || (data->fading == 1) || (data->fading == 4)) {

		while (toread) {
			gint t;

			t = MIN (toread * CHANNELS * sizeof (xmms_samplefloat_t),
			         sizeof (tbuf));

			if (output_is_ready_for_period(output, t) == 0) {
				res = xmms_output_read (output, (gchar *)tbuf, t);
			} else {
				/* xmms_log_info ("Not Enough Bits in the Ring Buffer, its going to be a silent period..."); */
				break;
			}

			if (res <= 0) {
				XMMS_DBG ("output_read returned %d", res);
				break;
			}

			res /= CHANNELS * sizeof (xmms_samplefloat_t);
			for (i = 0; i < res; i++) {
				for (j = 0; j < CHANNELS; j++) {
					if(data->fading) {
						if(data->fading == 1) {
							fade_value = ((xmms_samplefloat_t)(db_to_value(fade_per_sample * data->faded_samples)));
						} else {
							fade_value = ((xmms_samplefloat_t)(db_to_value(fade_per_sample * (data->fade_samples - data->faded_samples))));
						}
						sample = tbuf[i*CHANNELS + j];
						sample_result = (sample * fade_value);
						buf[j][i] = sample_result;
					} else {
						buf[j][i] = tbuf[i*CHANNELS + j];
					}
				}
				if(data->fading) {
					data->faded_samples += 1;
				}
			}
			toread -= res;
		}
	}
	if((data->fading == 1) || (data->fading == 4)) {
		XMMS_DBG ("An example sample %f was faded by %fdb or %f of value resulting in a sample of %f faded samples at this time is %d", sample, (xmms_samplefloat_t)(fade_per_sample * data->faded_samples), fade_value, sample_result, data->faded_samples);
	}
	/* fill rest of buffer with silence */
	for (i = frames - toread; i < frames; i++) {
		for (j = 0; j < CHANNELS; j++) {
			buf[j][i] = 0.0;
		}
	}

	return 0;
}


static void
xmms_jack_shutdown (void *arg)
{
	xmms_output_t *output = (xmms_output_t*) arg;
	xmms_jack_data_t *data;
	xmms_error_t err;

	xmms_error_reset (&err);

	data = xmms_output_private_data_get (output);
	data->error = TRUE;

	xmms_error_set (&err, XMMS_ERROR_GENERIC, "jackd has been shutdown");
	xmms_output_set_error (output, &err);
}


static void
xmms_jack_error (const gchar *desc)
{
	xmms_log_error ("Jack reported error: %s", desc);
}

