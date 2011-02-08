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


#include "libfaded.c"

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

	gint total_samples_to_fade;
	gint current_faded_samples;
	gint fading_in;
	gint fading_out;
	gint new_song;
	gint jumping;
	gint total_samples_to_crossfade;
	gint crossfading;
	gint current_crossfaded_samples;
	xmms_samplefloat_t *crossfade_buffer_p[2]; // 2 channel hard code
	xmms_samplefloat_t crossfade_buffer[2*16384];
	xmms_samplefloat_t zero_buffer[2*16384];

	gboolean error;
	gboolean running;
} xmms_jack_data_t;


/*
 * Function prototypes
 */

static gboolean xmms_jack_plugin_setup (xmms_output_plugin_t *plugin);
static gboolean xmms_jack_new (xmms_output_t *output);
static void xmms_jack_destroy (xmms_output_t *output);
static gboolean xmms_jack_status (xmms_output_t *output, xmms_playback_status_t status);
static void xmms_jack_flush (xmms_output_t *output);

static int xmms_jack_process (jack_nframes_t frames, void *arg);
static void xmms_jack_shutdown (void *arg);
static void xmms_jack_error (const gchar *desc);


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

	g_return_val_if_fail (output, FALSE);
	data = g_new0 (xmms_jack_data_t, 1);

	data->total_samples_to_fade = 88200;
	data->current_faded_samples = 0;
	data->fading_in = 0;
	data->fading_out = 0;
		data->new_song = 0;
							data->jumping = 0;
	data->crossfading = 0;
	data->total_samples_to_crossfade = 16384;
	data->current_crossfaded_samples = 0;

	int i, j;

	for (i = 0; i < data->total_samples_to_crossfade; i++) {
		for (j = 0; j < CHANNELS; j++) {
			data->zero_buffer[i*CHANNELS + j] = 0.0f;
		}
	}

	xmms_output_private_data_set (output, data);

	if (!xmms_jack_connect (output, data)) {
		return FALSE;
	}

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

	gint resy;

/*	if (!xmms_jack_ports_connected (data) && !xmms_jack_connect_ports (data)) {
		return FALSE;
	}
*/

	if (status == 420) {
		XMMS_DBG ("I know I got jumped!");

		if ((output_is_ready_for_period(output, 1024 * 16 * 4 * 2) == 0)) {	
				XMMS_DBG ("Output is ready to give 16 x 1024 samples");
							// the 4 times two at the end here is the number of bytes per sample and the number of channels
							resy = xmms_output_eviler_read (output, (gchar *)data->crossfade_buffer, ((16 * 1024) * (4 * 2)), 1);
							XMMS_DBG ("Output Gave me %d", resy);
							data->crossfading = 1;
				data->jumping = 1;
		} else {
				XMMS_DBG ("Output is NOT ready to give 16 x 1024 samples");
		}
	} 

	if (status == 666) {
		XMMS_DBG ("I know I got seeked!");
		
		/*if ((output_is_ready_for_period(output, 1024 * 16 * 4 * 2) == 0)) {	
				XMMS_DBG ("Output is ready to give 16 x 1024 samples");
							// the 4 times two at the end here is the number of bytes per sample and the number of channels
							resy = xmms_output_peek (output, (gchar *)data->crossfade_buffer, ((16 * 1024) * (4 * 2)));
							XMMS_DBG ("Output Gave me %d", resy);
							data->crossfading = 1;
		} else {
				XMMS_DBG ("Output is NOT ready to give 16 x 1024 samples");
		}
*/
	} 


	if (status == 777) {
		XMMS_DBG ("Got new song in there is signal!");

		data->new_song = 1;
	} 


	if (status == XMMS_PLAYBACK_STATUS_PLAY) {
		data->fading_in = 1;
		data->fading_out = 0;
		if(data->current_faded_samples) {
			XMMS_DBG ("Fade Out Reversed to a Fade In!");
			data->current_faded_samples = data->total_samples_to_fade - data->current_faded_samples;
		}
		data->running = TRUE;
	} 

	if (status == XMMS_PLAYBACK_STATUS_PAUSE || status == XMMS_PLAYBACK_STATUS_STOP) {
		data->running = FALSE;
		data->fading_in = 0;
		data->fading_out = 1;
		if(data->current_faded_samples) {
			XMMS_DBG ("Fade In Reversed to a Fade Out!");
			data->current_faded_samples = data->total_samples_to_fade - data->current_faded_samples;
		}
	}

	return TRUE;
}


static void
xmms_jack_flush (xmms_output_t *output)
{
	/* do nothing... */
}


static int
xmms_jack_process (jack_nframes_t frames, void *arg)
{
	xmms_output_t *output = (xmms_output_t*) arg;
	xmms_jack_data_t *data;
	xmms_samplefloat_t *buf[CHANNELS];
	xmms_samplefloat_t tbuf[CHANNELS*1024];

	xmms_samplefloat_t newbuf[CHANNELS*1024];
	xmms_samplefloat_t newbuf2[CHANNELS*1024];

	gint i, j, res, toread;

	g_return_val_if_fail (output, -1);
	data = xmms_output_private_data_get (output);
	g_return_val_if_fail (data, -1);

	for (i = 0; i < CHANNELS; i++) {
		buf[i] = jack_port_get_buffer (data->ports[i], frames);
	}

	toread = frames;

	if (data->running || data->fading_in || data->fading_out) {
		while (toread) {
			gint t;

			t = MIN (toread * CHANNELS * sizeof (xmms_samplefloat_t),
			         sizeof (tbuf));

			if (output_is_ready_for_period(output, t) == 0) {
				if (data->crossfading && data->jumping == 1) {

									// if the output buffer isn't the new buffer yet, wait for it 
					if 	(data->new_song == 2) {
						res = xmms_output_read (output, (gchar *)tbuf, t);
					
					} else {

					if 	(data->new_song == 1) { data->new_song = 2; }


				res = 8192; // RESET RES!


						res /= CHANNELS * sizeof (xmms_samplefloat_t);
						for (i = 0; i < res; i++) {
							for (j = 0; j < CHANNELS; j++) {
								tbuf[i*CHANNELS + j] = data->zero_buffer[i*CHANNELS + j];
								}
						}

						res = 8192; // RESET RES!

					}


				} else {


										res = xmms_output_read (output, (gchar *)tbuf, t);

				}
				
			} else {
			  if (data->crossfading) {
					/* We are crossfading so we will want to output te fade down of the other stuff mixed with silence unitil
						output is ready, rather than simply just silence, so lets make up some silence usually just a few cycles */
						


						res = 8192; // RESET RES!


						res /= CHANNELS * sizeof (xmms_samplefloat_t);
						for (i = 0; i < res; i++) {
							for (j = 0; j < CHANNELS; j++) {
								tbuf[i*CHANNELS + j] = data->zero_buffer[i*CHANNELS + j];
								}
						}

						res = 8192; // RESET RES!



				} else {
					XMMS_DBG ("Not Enough Bits in the Ring Buffer, its going to be a silent period...");
					break;
				}				
			}

			if (res <= 0) {
				XMMS_DBG ("output_read returned %d", res);
				break;
			}

			// IF FADE IN OR OUT
			if (data->fading_in || data->fading_out) {
				if(data->current_faded_samples == 0) { XMMS_DBG ("Starting Fade"); }
		
				if(data->fading_in) { 
					XMMS_DBG ("Fading In ");
					fade_in_chunk(tbuf, data->current_faded_samples, 1024, data->total_samples_to_fade);
				} else {
					XMMS_DBG ("Fading Out");
					fade_out_chunk(tbuf, data->current_faded_samples, 1024, data->total_samples_to_fade);
				}

				if(data->current_faded_samples < data->total_samples_to_fade) { 
					// hardcoded fix this
					data->current_faded_samples += 1024;
				}

				if(data->current_faded_samples >= data->total_samples_to_fade) { 
					data->fading_in = 0;
					data->fading_out = 0;
					data->current_faded_samples = 0;
					XMMS_DBG ("Fade Complete"); 
				}
			}

			// IF CROSSFADING
			if (data->crossfading) {
				if(data->current_crossfaded_samples == 0) { XMMS_DBG ("Starting Crossfade"); }


				// Do it
				XMMS_DBG ("Crossfading");

			// get a new buffer to pull from

			res /= CHANNELS * sizeof (xmms_samplefloat_t);
			for (i = 0; i < res; i++) {
				for (j = 0; j < CHANNELS; j++) {
					newbuf[i*CHANNELS + j] = data->crossfade_buffer[((data->current_crossfaded_samples + i) * CHANNELS) + j];
				}
			}

			res = 8192; // RESET RES!


			res /= CHANNELS * sizeof (xmms_samplefloat_t);
			for (i = 0; i < res; i++) {
				for (j = 0; j < CHANNELS; j++) {
					newbuf2[i*CHANNELS + j] = tbuf[i*CHANNELS + j];
				}
			}

			res = 8192; // RESET RES!



				crossfade_chunk(newbuf, newbuf2, tbuf, data->current_crossfaded_samples, 1024, data->total_samples_to_crossfade);


				if(data->current_crossfaded_samples < data->total_samples_to_crossfade) { 
					// hardcoded fix this
					data->current_crossfaded_samples += 1024;
				}

				if(data->current_crossfaded_samples >= data->total_samples_to_crossfade) { 
					data->crossfading = 0;
					data->current_crossfaded_samples = 0;
					XMMS_DBG ("CrossFade Complete"); 
							data->new_song = 0;
							data->jumping = 1;
				}

			}

			res /= CHANNELS * sizeof (xmms_samplefloat_t);
			for (i = 0; i < res; i++) {
				for (j = 0; j < CHANNELS; j++) {
					buf[j][i] = tbuf[i*CHANNELS + j];
				}
			}
			toread -= res;
		}
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

