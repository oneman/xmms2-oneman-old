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


/**
 * @file
 * Output plugin helper
 */


#include <string.h>
#include <unistd.h>

#include "xmmspriv/xmms_output.h"
#include "xmmspriv/xmms_ringbuf.h"
#include "xmmspriv/xmms_plugin.h"
#include "xmmspriv/xmms_xform.h"
#include "xmmspriv/xmms_sample.h"
#include "xmmspriv/xmms_medialib.h"
#include "xmmspriv/xmms_outputplugin.h"
#include "xmms/xmms_log.h"
#include "xmms/xmms_ipc.h"
#include "xmms/xmms_object.h"
#include "xmms/xmms_config.h"

#include "xmmspriv/xmms_fade.h"

#define VOLUME_MAX_CHANNELS 128

typedef enum xmms_output_filler_message_E {
	STOP,		/* Stops filling, and dumps the buffer. */
	RUN,		/* Filler is running, but often _waiting on ringbuf to have free space */
	QUIT,		/* This actually ends the output filler thread */	
	TICKLE,		/* Manual track change or tickle the filler to the next track */
	SEEK,
	NOOP,		/* this is for convience */
} xmms_output_filler_message_t, xmms_output_filler_state_t;


typedef struct xmms_volume_map_St {
	const gchar **names;
	guint *values;
	guint num_channels;
	gboolean status;
} xmms_volume_map_t;

static gboolean xmms_output_format_set (xmms_output_t *output, xmms_stream_type_t *fmt);
static gpointer xmms_output_monitor_volume_thread (gpointer data);

static void xmms_playback_client_start (xmms_output_t *output, xmms_error_t *err);
static void xmms_playback_client_stop (xmms_output_t *output, xmms_error_t *err);
static void xmms_playback_client_pause (xmms_output_t *output, xmms_error_t *err);
static void xmms_playback_client_xform_kill (xmms_output_t *output, xmms_error_t *err);
static void xmms_playback_client_seekms (xmms_output_t *output, gint32 ms, gint32 whence, xmms_error_t *error);
static void xmms_playback_client_seeksamples (xmms_output_t *output, gint32 samples, gint32 whence, xmms_error_t *error);
static gint32 xmms_playback_client_status (xmms_output_t *output, xmms_error_t *error);
static gint xmms_playback_client_current_id (xmms_output_t *output, xmms_error_t *error);
static gint32 xmms_playback_client_playtime (xmms_output_t *output, xmms_error_t *err);

static void xmms_playback_client_volume_set (xmms_output_t *output, const gchar *channel, gint32 volume, xmms_error_t *error);
static GTree *xmms_playback_client_volume_get (xmms_output_t *output, xmms_error_t *error);
static void xmms_output_filler_message (xmms_output_t *output, xmms_output_filler_message_t message);
static void fade_complete (xmms_output_t *output);

static void xmms_volume_map_init (xmms_volume_map_t *vl);
static void xmms_volume_map_free (xmms_volume_map_t *vl);
static void xmms_volume_map_copy (xmms_volume_map_t *src, xmms_volume_map_t *dst);
static GTree *xmms_volume_map_to_dict (xmms_volume_map_t *vl);
static gboolean xmms_output_status_set (xmms_output_t *output, gint status);
static gboolean set_plugin (xmms_output_t *output, xmms_output_plugin_t *plugin);

static void xmms_output_format_list_free_elem (gpointer data, gpointer user_data);
static void xmms_output_format_list_clear (xmms_output_t *output);
xmms_medialib_entry_t xmms_output_current_id (xmms_output_t *output);

XMMS_CMD_DEFINE (start, xmms_playback_client_start, xmms_output_t *, NONE, NONE, NONE);
XMMS_CMD_DEFINE (stop, xmms_playback_client_stop, xmms_output_t *, NONE, NONE, NONE);
XMMS_CMD_DEFINE (pause, xmms_playback_client_pause, xmms_output_t *, NONE, NONE, NONE);
XMMS_CMD_DEFINE (xform_kill, xmms_playback_client_xform_kill, xmms_output_t *, NONE, NONE, NONE);
XMMS_CMD_DEFINE (playtime, xmms_playback_client_playtime, xmms_output_t *, INT32, NONE, NONE);
XMMS_CMD_DEFINE (seekms, xmms_playback_client_seekms, xmms_output_t *, NONE, INT32, INT32);
XMMS_CMD_DEFINE (seeksamples, xmms_playback_client_seeksamples, xmms_output_t *, NONE, INT32, INT32);
XMMS_CMD_DEFINE (output_status, xmms_playback_client_status, xmms_output_t *, INT32, NONE, NONE);
XMMS_CMD_DEFINE (currentid, xmms_playback_client_current_id, xmms_output_t *, INT32, NONE, NONE);
XMMS_CMD_DEFINE (volume_set, xmms_playback_client_volume_set, xmms_output_t *, NONE, STRING, INT32);
XMMS_CMD_DEFINE (volume_get, xmms_playback_client_volume_get, xmms_output_t *, DICT, NONE, NONE);

/*
 * Type definitions
 */

/** @defgroup Output Output
  * @ingroup XMMSServer
  * @brief Output is responsible to put the decoded data on
  * the soundcard.
  * @{
  */

struct xmms_output_St {
	xmms_object_t object;

	/* temp */
	int fade;
	int sample_start_number;
	int total_samples;
	int in_or_out;
	guint8 fadebuffer[64 * 4096];
	guint8 ffadebuffer[64 * 4096];
	int crossfade;
	int crossfade_total;
	xmms_output_plugin_t *plugin;
	gpointer plugin_data;

	gint played;
	gint played_time;
	
	xmms_medialib_entry_t current_entry;
	guint toskip;
	gint where_is_the_output_filler;
	/* */

	xmms_ringbuf_t *filler_messages;
	gboolean tickled_when_paused;
	gint chunksize;

	GThread *filler_thread;
	GMutex *filler_mutex;
	GMutex *read_mutex;

	xmms_output_filler_state_t filler_state;
	xmms_output_filler_state_t new_internal_filler_state;

	xmms_ringbuf_t *filler_buffer;
	xmms_ringbuf_t *filler_bufferA;
	xmms_ringbuf_t *filler_bufferB;
	xmms_ringbuf_t *inactive_filler_buffer;
	gboolean switchbuffer_seek;
	int output_needs_to_switch_buffers;
	gint switchcount;

	guint32 filler_seek;
	gint filler_skip;

	/** Internal status, tells which state the
	    output really is in */

	GMutex *status_mutex;
	guint status;

	xmms_playlist_t *playlist;

	/** Supported formats */
	GList *format_list;
	/** Active format */
	xmms_stream_type_t *format;

	/**
	 * How many times didn't we have enough data in the buffer?
	 */
	gint32 buffer_underruns;

	GThread *monitor_volume_thread;
	gboolean monitor_volume_running;
};

/** @} */

/*
 * Public functions
 */

gpointer
xmms_output_private_data_get (xmms_output_t *output)
{
	g_return_val_if_fail (output, NULL);
	g_return_val_if_fail (output->plugin, NULL);

	return output->plugin_data;
}

void
xmms_output_private_data_set (xmms_output_t *output, gpointer data)
{
	g_return_if_fail (output);
	g_return_if_fail (output->plugin);

	output->plugin_data = data;
}

void
xmms_output_stream_type_add (xmms_output_t *output, ...)
{
	xmms_stream_type_t *f;
	va_list ap;

	va_start (ap, output);
	f = xmms_stream_type_parse (ap);
	va_end (ap);

	g_return_if_fail (f);

	output->format_list = g_list_append (output->format_list, f);
}

static void
xmms_output_format_list_free_elem (gpointer data, gpointer user_data)
{
	xmms_stream_type_t *f;

	g_return_if_fail (data);

	f = data;

	xmms_object_unref (f);
}

static void
xmms_output_format_list_clear(xmms_output_t *output)
{
	if (output->format_list == NULL)
		return;

	g_list_foreach (output->format_list,
	                xmms_output_format_list_free_elem,
	                NULL);

	g_list_free (output->format_list);
	output->format_list = NULL;
}

static void
update_playtime (xmms_output_t *output, int advance)
{
	guint buffersize = 0;

	g_atomic_int_add(&output->played, advance);

	gint played = g_atomic_int_get(&output->played);

	buffersize = xmms_output_plugin_method_latency_get (output->plugin, output);

	if (played < buffersize) {
		buffersize = played;
	}

	if (output->format) {
		guint ms = xmms_sample_bytes_to_ms (output->format,
		                                    played - buffersize);
		/* This I think prevents sending signal if samples read was less than 50~ */
		if ((ms / 100) != (g_atomic_int_get(&output->played_time) / 100) || (advance == 0)) {
			xmms_object_emit_f (XMMS_OBJECT (output),
			                    XMMS_IPC_SIGNAL_PLAYBACK_PLAYTIME,
			                    XMMSV_TYPE_INT32,
			                    ms);
		}
		g_atomic_int_set(&output->played_time, ms);

	}

}

void
xmms_output_set_error (xmms_output_t *output, xmms_error_t *error)
{
	g_return_if_fail (output);

	xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_STOP);

	if (error) {
		xmms_log_error ("Output plugin %s reported error, '%s'",
		                xmms_plugin_shortname_get ((xmms_plugin_t *)output->plugin),
		                xmms_error_message_get (error));
	}
}

typedef struct {
	xmms_output_t *output;
	xmms_xform_t *chain;
	gboolean flush;
	gboolean emit;
} xmms_output_song_changed_arg_t;

static void
song_changed_arg_free (void *data)
{
	xmms_output_song_changed_arg_t *arg = (xmms_output_song_changed_arg_t *)data;
	xmms_object_unref (arg->chain);
	g_free (arg);
}

static gboolean
song_changed (void *data)
{
	/* executes in the output thread; NOT the filler thread */
	xmms_output_song_changed_arg_t *arg = (xmms_output_song_changed_arg_t *)data;
	xmms_medialib_entry_t entry;
	xmms_stream_type_t *type;

	entry = xmms_xform_entry_get (arg->chain);

	XMMS_DBG ("Running hotspot! Song changed!! %d", entry);

	g_atomic_int_set(&arg->output->played, 0);
	arg->output->current_entry = entry;

	type = xmms_xform_outtype_get (arg->chain);

	if (!xmms_output_format_set (arg->output, type)) {
		gint fmt, rate, chn;

		fmt = xmms_stream_type_get_int (type, XMMS_STREAM_TYPE_FMT_FORMAT);
		rate = xmms_stream_type_get_int (type, XMMS_STREAM_TYPE_FMT_SAMPLERATE);
		chn = xmms_stream_type_get_int (type, XMMS_STREAM_TYPE_FMT_CHANNELS);

		XMMS_DBG ("Couldn't set format %s/%d/%d, stopping filler..",
		          xmms_sample_name_get (fmt), rate, chn);

		xmms_output_filler_message (arg->output, STOP);
		return FALSE;
	}

	if (arg->flush)
		xmms_output_flush (arg->output);

	if (arg->emit)
	xmms_object_emit_f (XMMS_OBJECT (arg->output),
	                    XMMS_IPC_SIGNAL_PLAYBACK_CURRENTID,
	                    XMMSV_TYPE_INT32,
	                    entry);

	return TRUE;
}

static gboolean
seek_done (void *data)
{
	xmms_output_t *output = (xmms_output_t *)data;

	g_atomic_int_set(&output->played, output->filler_seek * xmms_sample_frame_size_get (output->format));
	output->toskip = output->filler_skip * xmms_sample_frame_size_get (output->format);

	return TRUE;
}


static gboolean
seek_done_noskip (void *data)
{
	xmms_output_t *output = (xmms_output_t *)data;

	g_atomic_int_set(&output->played, output->filler_seek * xmms_sample_frame_size_get (output->format));

	return TRUE;
}

static void
xmms_output_filler_seek (xmms_output_t *output, gint samples)
{

	xmms_output_filler_message_t message;

	g_atomic_int_set(&output->filler_seek, samples);
	message = SEEK;

	xmms_output_filler_message(output, message);

}

static void
xmms_output_filler_message (xmms_output_t *output, xmms_output_filler_message_t message)
{
	int buf[1];
	
	buf[0] = message;
	xmms_ringbuf_write(output->filler_messages, buf, 4);
}

static xmms_output_filler_state_t
xmms_output_filler_check_for_message(xmms_output_t *output) {

	int ret;
	int buf[1];

	ret = xmms_ringbuf_read(output->filler_messages, buf, 4);

	if(ret > 0) {
		output->filler_state = buf[0];
		return output->filler_state;
	}

	return NOOP;

}

static xmms_output_filler_state_t
xmms_output_filler_wait_for_message(xmms_output_t *output) {

	int ret;
	int buf[1];

	ret = xmms_ringbuf_read_wait(output->filler_messages, buf, 4, output->filler_mutex);

	if(ret > 0) {
		output->filler_state = buf[0];
		return output->filler_state;
	}

	return NOOP;

}


static xmms_output_filler_state_t
xmms_output_filler_wait_for_message_or_space(xmms_output_t *output) {

	int ret;
	int buf[1];
	int free_bytes;

	while(TRUE) {

		ret = xmms_ringbuf_timed_wait_used(output->filler_messages, 4, output->filler_mutex, 30);

		if (ret == TRUE) {
		
			ret = xmms_ringbuf_read(output->filler_messages, buf, 4);
		
			if(ret > 0) {
				output->filler_state = buf[0];
				return output->filler_state;
			}
			
		} else {
			free_bytes = xmms_ringbuf_bytes_free(output->filler_buffer);
			if(free_bytes >= output->chunksize){
				output->filler_state = RUN;
				return output->filler_state;
			}
		
		}
	}

}

static void *
xmms_output_filler (void *arg)
{
	xmms_output_t *output = (xmms_output_t *)arg;
	xmms_xform_t *chain = NULL;
	char buf[output->chunksize];
	xmms_error_t err;
	gint ret;

	xmms_ringbuf_vector_t ringbuf_write_vectors[2];

	xmms_error_reset (&err);

	xmms_medialib_entry_t entry;
	xmms_output_song_changed_arg_t *hsarg;
	xmms_medialib_session_t *session;

	while (output->filler_state != QUIT) {

		/* Check for new state, first internally determined, then externally commanded */
		if(output->new_internal_filler_state != NOOP) {
			output->filler_state = output->new_internal_filler_state;
			output->new_internal_filler_state = NOOP;
		} else {
			if(xmms_output_filler_check_for_message(output) != NOOP) {
				XMMS_DBG ("Output Filler Received New State: %d", output->filler_state );
				continue;
			}
		}

		/* Stopped State */
		if (output->filler_state == STOP) {
			if (chain) {
				
				XMMS_DBG ("Output filler stopped, chain destroyed");
				xmms_object_unref (chain);
				chain = NULL;
			}
			/* remember, nothing is actually cleared, the read/write pointers are just set to zero, so if something is still reading at this moment
			   things will be ok */
			xmms_ringbuf_clear (output->filler_buffer);
			xmms_ringbuf_set_eos (output->filler_buffer, FALSE); 
			XMMS_DBG ("Output filler stopped and waiting..." );
			while(output->filler_state == STOP) {
				xmms_output_filler_wait_for_message(output);
			}	
			if (output->filler_state == RUN) {
				XMMS_DBG ("Stopped Output filler awakens and prepares to run");
			}
			if (output->filler_state == QUIT) {
				XMMS_DBG ("Stopped Output filler awakens and prepares to quit");
			}
			continue;
		}

		/* TICKLED State, aka Manual Track Change */

		if (output->filler_state == TICKLE) {
			if (chain) {
				xmms_object_unref (chain);
				chain = NULL;
				output->new_internal_filler_state = RUN;
				XMMS_DBG ("Chain destroyed");

			// switchbuffer jump
				if (output->status == 1) {
					output->switchbuffer_seek = TRUE;
					output->inactive_filler_buffer = (xmms_ringbuf_t *)xmms_output_get_inactive_buffer(output);
				}
			} else {
				XMMS_DBG ("Filler Kill without chain requested, going to stop mode");
				output->new_internal_filler_state = STOP;
			}

			if (output->status == 2) {
				output->tickled_when_paused = TRUE;
				xmms_ringbuf_clear (output->filler_buffer);
				//g_atomic_int_set(&output->played, 0);
				//update_playtime (output, 0);
			}

			continue;
		}

		/* SEEK State */

		if (output->filler_state == SEEK) {
			if (!chain) {
				XMMS_DBG ("Seek without chain, ignoring..");
				output->new_internal_filler_state = STOP;
				continue;
			}

			ret = xmms_xform_this_seek (chain, output->filler_seek, XMMS_XFORM_SEEK_SET, &err);
			if (ret == -1) {
				XMMS_DBG ("Seeking failed: %s", xmms_error_message_get (&err));
			} else {
				XMMS_DBG ("Seek ok! %d", ret);

				output->filler_skip = output->filler_seek - ret;
				if (output->filler_skip < 0) {
					XMMS_DBG ("Seeked %d samples too far! Updating position...",
					          -output->filler_skip);

					output->filler_skip = 0;
					output->filler_seek = ret;
				}

				/* If we are seeking when paused */
				if (output->status == 2) {
					if(output->tickled_when_paused == TRUE) {
					// to ensure we dont end up with a series of seeks, we clear the ringbuf
					// set the song changed hotspot again, and then set a seek hotspot
					// we also update the playtime tho it will flux momentarily when the songplayed hotspot kicks
					xmms_ringbuf_clear (output->filler_buffer);
					hsarg = g_new0 (xmms_output_song_changed_arg_t, 1);
					hsarg->output = output;
					hsarg->chain = chain;
					hsarg->flush = TRUE;
					hsarg->emit = FALSE;
					xmms_object_ref (chain);
					g_atomic_int_set(&output->played, output->filler_seek * xmms_sample_frame_size_get (output->format));
					update_playtime (output, 0);
					xmms_ringbuf_hotspot_set (output->filler_buffer, song_changed, song_changed_arg_free, hsarg);
					xmms_ringbuf_hotspot_set (output->filler_buffer, seek_done, NULL, output);

					} else {
						xmms_ringbuf_clear (output->filler_buffer);
						// We dont want to nuke any song changed callbacks incase we have changed songs
						// if there was a seek callback waiting (unlikely) well thats going to be wierd
						// the song changed callback will unset our playtime momentarily when it hits
						/* The following 2 lines are the same as the seek_done callback */
						g_atomic_int_set(&output->played, output->filler_seek * xmms_sample_frame_size_get (output->format));
						output->toskip = output->filler_skip * xmms_sample_frame_size_get (output->format);
						/* We update playtime to send out the playtime signal */
						update_playtime (output, 0);
					}
				}

					output->switchbuffer_seek = TRUE;
					output->inactive_filler_buffer = (xmms_ringbuf_t *)xmms_output_get_inactive_buffer(output);
					output->toskip = output->filler_skip * xmms_sample_frame_size_get (output->format);
					xmms_ringbuf_hotspot_set (output->inactive_filler_buffer, seek_done_noskip, NULL, output);

			}
			output->new_internal_filler_state = RUN;
			continue;
		}

		/* Running State, If we have no chain at this time */

		if (!chain) {
			XMMS_DBG ("No current chain, attempting to setup a new chain");

			entry = xmms_playlist_current_entry (output->playlist);
			if (!entry) {
				XMMS_DBG ("No entry from playlist!");
				output->new_internal_filler_state = STOP;
				continue;
			}
			chain = xmms_xform_chain_setup (entry, output->format_list, FALSE);
			if (!chain) {
				XMMS_DBG ("New chain failed");
				session = xmms_medialib_begin_write ();
				if (xmms_medialib_entry_property_get_int (session, entry, XMMS_MEDIALIB_ENTRY_PROPERTY_STATUS) == XMMS_MEDIALIB_ENTRY_STATUS_NEW) {
					xmms_medialib_end (session);
					xmms_medialib_entry_remove (entry);
				} else {
					xmms_medialib_entry_status_set (session, entry, XMMS_MEDIALIB_ENTRY_STATUS_NOT_AVAILABLE);
					xmms_medialib_entry_send_update (entry);
					xmms_medialib_end (session);
				}

				if (!xmms_playlist_advance (output->playlist)) {
					XMMS_DBG ("End of playlist");
					xmms_ringbuf_set_eos (output->filler_buffer, TRUE); 
				}
				continue;
			}

			hsarg = g_new0 (xmms_output_song_changed_arg_t, 1);
			hsarg->output = output;
			hsarg->chain = chain;
			if(output->status != 2) {
				hsarg->emit = TRUE;
			} else {
				hsarg->emit = FALSE;

				xmms_object_emit_f (XMMS_OBJECT (output),
	                    		XMMS_IPC_SIGNAL_PLAYBACK_CURRENTID,
	                    		XMMSV_TYPE_INT32,
	                    		entry);

				g_atomic_int_set(&output->played, 0);
				update_playtime (output, 0);

			}
			hsarg->flush = output->tickled_when_paused;
			xmms_object_ref (chain);
			XMMS_DBG ("New chain ready");
			if(output->switchbuffer_seek == TRUE) {
				xmms_ringbuf_hotspot_set (output->inactive_filler_buffer, song_changed, song_changed_arg_free, hsarg);
			} else {
				xmms_ringbuf_hotspot_set (output->filler_buffer, song_changed, song_changed_arg_free, hsarg);
			}
		}


		/* Running State and we have a chain */

		ret = xmms_xform_this_read (chain, buf, sizeof (buf), &err);
		if (ret > 0) {
			int wrote;
			wrote = 0;

			gint skip = MIN (ret, output->toskip);
			if (skip > 0) {
				XMMS_DBG ("Skip Num Bytes from seek was: %d", skip );
			}
			output->toskip -= skip;

			if (ret > skip) {
				if(output->switchbuffer_seek == TRUE) {
					wrote = xmms_ringbuf_write (output->inactive_filler_buffer, buf + skip, ret - skip);
				} else {
					wrote = xmms_ringbuf_write (output->filler_buffer, buf + skip, ret - skip);
				}
				
				while (wrote < (ret - skip)) {
					xmms_output_filler_wait_for_message_or_space(output);
					if (output->filler_state != RUN) {
						break;
					}
					wrote += xmms_ringbuf_write (output->filler_buffer, buf + skip + wrote, ret - skip - wrote);
				}
				if (output->filler_state != RUN) {
					XMMS_DBG ("State changed while waiting... %d", output->filler_state );
					continue;
				}

				if (output->switchbuffer_seek == TRUE) {
					output->switchcount++;
					if (output->switchcount < 5) {
						output->new_internal_filler_state = RUN;
						continue;
					} else {
						output->switchcount = 0;
						output->switchbuffer_seek = FALSE;
						output->output_needs_to_switch_buffers = TRUE;
						xmms_ringbuf_set_eos(output->filler_buffer, TRUE);
						XMMS_DBG ("Switching buffers! ");
						while(g_atomic_int_get(&output->output_needs_to_switch_buffers) == TRUE) {
							g_usleep(12000);
						}
					}
				}
			}

		} else {

			if (ret == -1) {
				XMMS_DBG ("Failed to get samples from chain");
				/* print error */
				xmms_error_reset (&err);
			}
			XMMS_DBG ("Got nothing more from chain, destroying it");
			xmms_object_unref (chain);
			chain = NULL;
			/* A natural track progression or we have hit the end of the playlist */
			if (!xmms_playlist_advance (output->playlist)) {
				XMMS_DBG ("End of playlist");
				xmms_ringbuf_set_eos (output->filler_buffer, TRUE);
			}
		}

	}

	/* Filler has been told to quit (xmms2d has been quit) */

	XMMS_DBG ("Filler thread says buh bye ;)");
	return NULL;
}

void
xmms_output_switchbuffers(xmms_output_t *output)
{

	if(output->filler_buffer == output->filler_bufferA) {
		output->filler_buffer = output->filler_bufferB;
		xmms_ringbuf_clear (output->filler_bufferA);
		xmms_ringbuf_set_eos(output->filler_bufferA, FALSE);
	} else {
		output->filler_buffer = output->filler_bufferA;
		xmms_ringbuf_clear (output->filler_bufferB);
		xmms_ringbuf_set_eos(output->filler_bufferB, FALSE);
	}

}


void
fade_complete(xmms_output_t *output)
{

	if (output->fade == 1) {
		output->fade = 2;
		xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_PAUSE);
	} else {

		if (output->fade == 2) {
			output->fade = 0;
		}
	}
	
	output->sample_start_number = 0;

}

void *
xmms_output_get_inactive_buffer(xmms_output_t *output)
{

	if(output->filler_buffer == output->filler_bufferA) {
		xmms_ringbuf_clear (output->filler_bufferB);
		return output->filler_bufferB;
	} else {
		xmms_ringbuf_clear (output->filler_bufferA);
		return output->filler_bufferA;
	}

}

void
xmms_output_get_vectors(xmms_output_t *output, xmms_output_vector_t * vectors)
{
	xmms_ringbuf_get_read_vector (output->filler_buffer, (xmms_ringbuf_vector_t *) vectors);
}

void
xmms_output_advance(xmms_output_t *output, gint cnt)
{
	xmms_ringbuf_read_advance(output->filler_buffer, cnt);
	update_playtime (output, cnt);
}

gint
xmms_output_get_next_hotspot_pos(xmms_output_t *output)
{
	return xmms_ringbuf_get_next_hotspot_pos (output->filler_buffer);
}

gint
xmms_output_get_ringbuf_pos(xmms_output_t *output)
{
	return xmms_ringbuf_get_read_pos (output->filler_buffer);
}

void
xmms_output_hit_hotspot(xmms_output_t *output)
{
	xmms_ringbuf_hit_hotspot (output->filler_buffer);
}



gint
xmms_output_read (xmms_output_t *output, char *buffer, gint len)
{
	gint ret;
	xmms_error_t err;

	xmms_error_reset (&err);

	g_return_val_if_fail (output, -1);
	g_return_val_if_fail (buffer, -1);



	if(output->output_needs_to_switch_buffers == TRUE) {
		output->crossfade = xmms_ringbuf_read (output->filler_buffer, output->fadebuffer, 64 * 4096);

		
		xmms_output_switchbuffers(output);
		g_atomic_int_set(&output->output_needs_to_switch_buffers, 0);
		
		ret = xmms_ringbuf_read (output->filler_buffer, buffer, len);
		
		if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S16)
		{
		output->crossfade = output->crossfade / 2;
		output->crossfade_total = output->crossfade;
			crossfade_chunk_s16(output->fadebuffer, buffer, buffer, 0, len / 2, output->crossfade_total);
	output->crossfade = output->crossfade - len / 2;
		}
				if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S32)
		{
		output->crossfade = output->crossfade / 4;
		output->crossfade_total = output->crossfade;
				crossfade_chunk_s32(output->fadebuffer, buffer, buffer, 0, len / 4, output->crossfade_total);
	output->crossfade = output->crossfade - len / 4;
		}
				if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_FLOAT)
		{
		output->crossfade = output->crossfade / 4;
		output->crossfade_total = output->crossfade;
				crossfade_chunk(output->fadebuffer, buffer, buffer, 0, len / 4, output->crossfade_total);
	output->crossfade = output->crossfade - len / 4;			
		}
		
	} else {

	if (!((output->fade == 1) && (output->sample_start_number >= output->total_samples))) {
	
		if(output->crossfade > 0) {		
		
			ret = xmms_ringbuf_read (output->filler_buffer, buffer, len);
			



			
			
					if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S16)
		{
			crossfade_chunk_s16(&output->fadebuffer, buffer, buffer, output->crossfade_total - output->crossfade, len / 2, output->crossfade_total);
	output->crossfade = output->crossfade - len / 2;
		}
				if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S32)
		{
			crossfade_chunk_s32(&output->fadebuffer, buffer, buffer, output->crossfade_total - output->crossfade, len / 4, output->crossfade_total);
	output->crossfade = output->crossfade - len / 4;
		}
				if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_FLOAT)
		{
		

			crossfade_chunk(&output->fadebuffer, buffer, buffer, output->crossfade_total - output->crossfade, len / 4, output->crossfade_total);
	output->crossfade = output->crossfade - len / 4;			
		}
			
			
			
			if (output->crossfade < len / 8) {
					output->crossfade = 0;
					output->crossfade_total = 0;
			}
			
		} else {
			ret = xmms_ringbuf_read (output->filler_buffer, buffer, len);
		}

		}
	}
	
	
	if(output->fade) {
	
	if (output->sample_start_number < output->total_samples) {
	
		//XMMS_DBG ("Fade Chunk: SSN: %d LEN: %d TOTAL: %d INOROUT: %d", output->sample_start_number, ret / 2, output->total_samples, output->fade - 1);
		
		if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_FLOAT)
		{
			//XMMS_DBG ("got float");
			fade_chunk(buffer, output->sample_start_number, ret, output->total_samples, output->fade - 1);
			output->sample_start_number += ret / 4;
		}
		
		if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S32)
		{
			//XMMS_DBG ("got s32");
			fade_chunk_s32(buffer, output->sample_start_number, ret / 4, output->total_samples, output->fade - 1);
			output->sample_start_number += ret / 4;
		}
	
		if(xmms_stream_type_get_int(output->format, XMMS_STREAM_TYPE_FMT_FORMAT) == XMMS_SAMPLE_FORMAT_S16)
		{
			//XMMS_DBG ("got s16");
			fade_chunk_s16(buffer, output->sample_start_number, ret / 2, output->total_samples, output->fade - 1);
			output->sample_start_number += ret / 2;
		}
		
	} else {
			output->sample_start_number += len / 4;
			if ((output->fade - 1) == 0) {
				int x;
				for(x = 0; x < ret; x++)
					buffer[x] = 0;
				if (output->sample_start_number >= (output->total_samples + 8192 * 12)) {
					fade_complete(output);
				}
			} else {
				fade_complete(output);
			}
		}
	}
	
	
	if (ret == 0 && xmms_ringbuf_iseos (output->filler_buffer)) {
		xmms_output_filler_message (output, STOP);
		xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_STOP);
		return -1;
	}

	if (!((output->fade) && (output->sample_start_number >= output->total_samples)))
		update_playtime (output, ret);

	return ret;
}

gint
xmms_output_read_wait (xmms_output_t *output, char *buffer, gint len)
{
	xmms_ringbuf_wait_used (output->filler_buffer, len, output->read_mutex);
	return xmms_output_read (output, buffer, len);
}



guint
xmms_output_bytes_available (xmms_output_t *output)
{
	//if (output->output_needs_to_switch_buffers == TRUE) {
	//	xmms_output_switchbuffers(output);
	//	g_atomic_int_set(&output->output_needs_to_switch_buffers, 0);
	//}

	return xmms_ringbuf_bytes_used(output->filler_buffer);
}

xmms_config_property_t *
xmms_output_config_property_register (xmms_output_t *output, const gchar *name, const gchar *default_value, xmms_object_handler_t cb, gpointer userdata)
{
	g_return_val_if_fail (output->plugin, NULL);
	return xmms_plugin_config_property_register ((xmms_plugin_t *)output->plugin, name, default_value, cb, userdata);
}

xmms_config_property_t *
xmms_output_config_lookup (xmms_output_t *output, const gchar *path)
{
	g_return_val_if_fail (output->plugin, NULL);
	return xmms_plugin_config_lookup ((xmms_plugin_t *)output->plugin, path);
}

xmms_medialib_entry_t
xmms_output_current_id (xmms_output_t *output)
{
	g_return_val_if_fail (output, 0);
	return output->current_entry;
}


/** @addtogroup Output
 * @{
 */
/** Methods */
static void
xmms_playback_client_xform_kill (xmms_output_t *output, xmms_error_t *error)
{
	xmms_output_filler_message (output, TICKLE);
}

static void
xmms_playback_client_seekms (xmms_output_t *output, gint32 ms, gint32 whence, xmms_error_t *error)
{
	guint samples;

	g_return_if_fail (output);

	if (whence == XMMS_PLAYBACK_SEEK_CUR) {

		ms += g_atomic_int_get(&output->played_time);
		if (ms < 0) {
			ms = 0;
		}

	}

	if (output->format) {
		samples = xmms_sample_ms_to_samples (output->format, ms);

		xmms_playback_client_seeksamples (output, samples,
		                                  XMMS_PLAYBACK_SEEK_SET, error);
	}
}

static void
xmms_playback_client_seeksamples (xmms_output_t *output, gint32 samples, gint32 whence, xmms_error_t *error)
{
	if (whence == XMMS_PLAYBACK_SEEK_CUR) {

		samples += g_atomic_int_get(&output->played) / xmms_sample_frame_size_get (output->format);
		if (samples < 0) {
			samples = 0;
		}

	}

	xmms_output_filler_seek (output, samples);

}

static void
xmms_playback_client_start (xmms_output_t *output, xmms_error_t *err)
{
	g_return_if_fail (output);

	printf("Output Buffer State: %d Ringbuffer Info: Size: %d Free: %d Used: %d\n", 
			output->where_is_the_output_filler,
			xmms_ringbuf_size(output->filler_buffer), 
			xmms_ringbuf_bytes_free(output->filler_buffer), 
			xmms_ringbuf_bytes_used(output->filler_buffer));

	output->tickled_when_paused = FALSE;
	xmms_output_filler_message (output, RUN);
	if (!xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_PLAY)) {
		xmms_output_filler_message (output, STOP);
		XMMS_DBG ("i friggin stopped playback");
		xmms_error_set (err, XMMS_ERROR_GENERIC, "Could not start playback");
	}

}

static void
xmms_playback_client_stop (xmms_output_t *output, xmms_error_t *err)
{
	g_return_if_fail (output);
	
	output->fade = 0;

	xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_STOP);

	xmms_output_filler_message (output, STOP);
}

static void
xmms_playback_client_pause (xmms_output_t *output, xmms_error_t *err)
{
	g_return_if_fail (output);

	output->fade = 1;

	//xmms_output_status_set (output, XMMS_PLAYBACK_STATUS_PAUSE);
}


static gint32
xmms_playback_client_status (xmms_output_t *output, xmms_error_t *error)
{
	gint32 ret;
	g_return_val_if_fail (output, XMMS_PLAYBACK_STATUS_STOP);

	g_mutex_lock (output->status_mutex);
	ret = output->status;
	g_mutex_unlock (output->status_mutex);
	return ret;
}

static gint
xmms_playback_client_current_id (xmms_output_t *output, xmms_error_t *error)
{
	return output->current_entry;
}

static void
xmms_playback_client_volume_set (xmms_output_t *output, const gchar *channel,
                               gint32 volume, xmms_error_t *error)
{

	if (!output->plugin) {
		xmms_error_set (error, XMMS_ERROR_GENERIC,
		                "couldn't set volume, output plugin not loaded");
		return;
	}

	if (!xmms_output_plugin_method_volume_set_available (output->plugin)) {
		xmms_error_set (error, XMMS_ERROR_GENERIC,
		                "operation not supported");
		return;
	}

	if (volume > 100) {
		xmms_error_set (error, XMMS_ERROR_INVAL, "volume out of range");
		return;
	}

	if (!xmms_output_plugin_methods_volume_set (output->plugin, output, channel, volume)) {
		xmms_error_set (error, XMMS_ERROR_GENERIC,
		                "couldn't set volume");
	}
}

static GTree *
xmms_playback_client_volume_get (xmms_output_t *output, xmms_error_t *error)
{
	GTree *ret;
	xmms_volume_map_t map;

	if (!output->plugin) {
		xmms_error_set (error, XMMS_ERROR_GENERIC,
		                "couldn't get volume, output plugin not loaded");
		return NULL;
	}

	if (!xmms_output_plugin_method_volume_get_available (output->plugin)) {
		xmms_error_set (error, XMMS_ERROR_GENERIC,
		                "operation not supported");
		return NULL;
	}

	xmms_error_set (error, XMMS_ERROR_GENERIC,
	                "couldn't get volume");

	xmms_volume_map_init (&map);

	/* ask the plugin how much channels it would like to set */
	if (!xmms_output_plugin_method_volume_get (output->plugin, output,
	                                           NULL, NULL, &map.num_channels)) {
		return NULL;
	}

	/* check for sane values */
	g_return_val_if_fail (map.num_channels > 0, NULL);
	g_return_val_if_fail (map.num_channels <= VOLUME_MAX_CHANNELS, NULL);

	map.names = g_new (const gchar *, map.num_channels);
	map.values = g_new (guint, map.num_channels);

	map.status = xmms_output_plugin_method_volume_get (output->plugin, output,
	                                                   map.names, map.values,
	                                                   &map.num_channels);

	if (!map.status || !map.num_channels) {
		return NULL; /* error is set (-> no leak) */
	}

	ret = xmms_volume_map_to_dict (&map);

	/* success! */
	xmms_error_reset (error);

	return ret;
}

/**
 * Get the current playtime in milliseconds.
 */
static gint32
xmms_playback_client_playtime (xmms_output_t *output, xmms_error_t *error)
{
	guint32 ret;
	g_return_val_if_fail (output, 0);

	ret = g_atomic_int_get(&output->played_time);

	return ret;
}

/* returns the current latency: time left in ms until the data currently read
 *                              from the latest xform in the chain will actually be played
 */
guint32
xmms_output_latency (xmms_output_t *output)
{
	guint ret = 0;
	guint buffersize = 0;

	if (output->format) {
		/* data already waiting in the ringbuffer */
		buffersize += xmms_ringbuf_bytes_used (output->filler_buffer);

		/* latency of the soundcard */
		buffersize += xmms_output_plugin_method_latency_get (output->plugin, output);

		ret = xmms_sample_bytes_to_ms (output->format, buffersize);
	}

	return ret;
}

/**
 * @internal
 */

static gboolean
xmms_output_status_set (xmms_output_t *output, gint status)
{
	gboolean ret = TRUE;

	if (!output->plugin) {
		XMMS_DBG ("No plugin to set status on..");
		return FALSE;
	}

	g_mutex_lock (output->status_mutex);

	if (output->status != status) {
		if (status == XMMS_PLAYBACK_STATUS_PAUSE &&
		    output->status != XMMS_PLAYBACK_STATUS_PLAY) {
			XMMS_DBG ("Can only pause from play.");
			ret = FALSE;
		} else {
			output->status = status;

			if (status == XMMS_PLAYBACK_STATUS_STOP) {
				xmms_object_unref (output->format);
				output->format = NULL;
			}
			if (!xmms_output_plugin_method_status (output->plugin, output, status)) {
				xmms_log_error ("Status method returned an error!");
				output->status = XMMS_PLAYBACK_STATUS_STOP;
				ret = FALSE;
			}

			xmms_object_emit_f (XMMS_OBJECT (output),
			                    XMMS_IPC_SIGNAL_PLAYBACK_STATUS,
			                    XMMSV_TYPE_INT32,
			                    output->status);
		}
	}

	g_mutex_unlock (output->status_mutex);

	return ret;
}

static void
xmms_output_destroy (xmms_object_t *object)
{
	xmms_output_t *output = (xmms_output_t *)object;

	output->monitor_volume_running = FALSE;
	if (output->monitor_volume_thread) {
		g_thread_join (output->monitor_volume_thread);
		output->monitor_volume_thread = NULL;
	}

	xmms_output_filler_message (output, QUIT);
	g_thread_join (output->filler_thread);


	if (output->plugin) {
		xmms_output_plugin_method_destroy (output->plugin, output);
		xmms_object_unref (output->plugin);
	}
	xmms_output_format_list_clear (output);

	xmms_object_unref (output->playlist);

	XMMS_DBG ("Freeing Filler Mutex");
	g_mutex_unlock (output->filler_mutex);
	g_mutex_free (output->filler_mutex);
	
	XMMS_DBG ("Freeing Status Mutex");
	g_mutex_free (output->status_mutex);
	XMMS_DBG ("Freeing Read Mutex");
	g_mutex_unlock (output->read_mutex);
	g_mutex_free (output->read_mutex);

	XMMS_DBG ("Freeing Ring Buffers");
	xmms_ringbuf_destroy (output->filler_bufferA);
	xmms_ringbuf_destroy (output->filler_bufferB);

	xmms_ringbuf_destroy (output->filler_messages);

	xmms_ipc_broadcast_unregister ( XMMS_IPC_SIGNAL_PLAYBACK_VOLUME_CHANGED);
	xmms_ipc_broadcast_unregister ( XMMS_IPC_SIGNAL_PLAYBACK_STATUS);
	xmms_ipc_broadcast_unregister ( XMMS_IPC_SIGNAL_PLAYBACK_CURRENTID);
	xmms_ipc_signal_unregister (XMMS_IPC_SIGNAL_PLAYBACK_PLAYTIME);
	xmms_ipc_object_unregister (XMMS_IPC_OBJECT_PLAYBACK);
}

/**
 * Switch to another output plugin.
 * @param output output pointer
 * @param new_plugin the new #xmms_plugin_t to use as output.
 * @returns TRUE on success and FALSE on failure
 */
gboolean
xmms_output_plugin_switch (xmms_output_t *output, xmms_output_plugin_t *new_plugin)
{
	xmms_output_plugin_t *old_plugin;
	gboolean ret;

	g_return_val_if_fail (output, FALSE);
	g_return_val_if_fail (new_plugin, FALSE);

	xmms_playback_client_stop (output, NULL);

	g_mutex_lock (output->status_mutex);

	old_plugin = output->plugin;

	ret = set_plugin (output, new_plugin);

	/* if the switch succeeded, release the reference to the old plugin
	 * now.
	 * if we couldn't switch to the new plugin, but we had a working
	 * plugin before, switch back to the old plugin.
	 */
	if (ret) {
		xmms_object_unref (old_plugin);
	} else if (old_plugin) {
		XMMS_DBG ("cannot switch plugin, going back to old one");
		set_plugin (output, old_plugin);
	}

	g_mutex_unlock (output->status_mutex);

	return ret;
}

/**
 * Allocate a new #xmms_output_t
 */
xmms_output_t *
xmms_output_new (xmms_output_plugin_t *plugin, xmms_playlist_t *playlist)
{
	xmms_output_t *output;
	xmms_config_property_t *prop;
	gint size;

	g_return_val_if_fail (playlist, NULL);

	XMMS_DBG ("Trying to open output");

	output = xmms_object_new (xmms_output_t, xmms_output_destroy);

	output->playlist = playlist;

	output->status_mutex = g_mutex_new ();

	prop = xmms_config_property_register ("output.buffersize", "32768", NULL, NULL);
	size = xmms_config_property_get_int (prop);
	XMMS_DBG ("Using buffersize %d", size);

	output->filler_messages = xmms_ringbuf_new (128);

	output->read_mutex = g_mutex_new ();
	g_mutex_lock (output->read_mutex); /* because it has to be locked or unlocked to be freed */

	output->filler_mutex = g_mutex_new ();
	g_mutex_lock (output->filler_mutex); /* because it has to be locked or unlocked to be freed */

	output->filler_state = STOP;
	output->crossfade = 0;
	output->crossfade_total = 0;
	output->chunksize = 4096;
	output->tickled_when_paused = FALSE;
	output->fade = 0;
	int y;

	for(y = 0; y < 64 * 4096; y++)
		output->ffadebuffer[y] = 0;


	output->new_internal_filler_state = NOOP;

	output->filler_bufferA = xmms_ringbuf_new (size);
	output->filler_bufferB = xmms_ringbuf_new (size);

	output->filler_buffer = output->filler_bufferA;
	output->switchbuffer_seek = FALSE;
	output->switchcount = 0;
	
	output->in_or_out = 1;
	output->sample_start_number = 0;
	output->total_samples = 8192 * 8;
	
	output->switchbuffer_seek = FALSE;
	output->output_needs_to_switch_buffers = FALSE;

	output->filler_thread = g_thread_create (xmms_output_filler, output, TRUE, NULL);

	xmms_config_property_register ("output.flush_on_pause", "1", NULL, NULL);
	xmms_ipc_object_register (XMMS_IPC_OBJECT_PLAYBACK, XMMS_OBJECT (output));

	/* Broadcasts are always transmitted to the client if he
	 * listens to them. */
	xmms_ipc_broadcast_register (XMMS_OBJECT (output),
	                             XMMS_IPC_SIGNAL_PLAYBACK_VOLUME_CHANGED);
	xmms_ipc_broadcast_register (XMMS_OBJECT (output),
	                             XMMS_IPC_SIGNAL_PLAYBACK_STATUS);
	xmms_ipc_broadcast_register (XMMS_OBJECT (output),
	                             XMMS_IPC_SIGNAL_PLAYBACK_CURRENTID);

	/* Signals are only emitted if the client has a pending question to it
	 * after the client recivies a signal, he must ask for it again */
	xmms_ipc_signal_register (XMMS_OBJECT (output),
	                          XMMS_IPC_SIGNAL_PLAYBACK_PLAYTIME);


	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_START,
	                     XMMS_CMD_FUNC (start));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_STOP,
	                     XMMS_CMD_FUNC (stop));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_PAUSE,
	                     XMMS_CMD_FUNC (pause));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_DECODER_KILL,
	                     XMMS_CMD_FUNC (xform_kill));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_CPLAYTIME,
	                     XMMS_CMD_FUNC (playtime));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_SEEKMS,
	                     XMMS_CMD_FUNC (seekms));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_SEEKSAMPLES,
	                     XMMS_CMD_FUNC (seeksamples));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_PLAYBACK_STATUS,
	                     XMMS_CMD_FUNC (output_status));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_CURRENTID,
	                     XMMS_CMD_FUNC (currentid));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_VOLUME_SET,
	                     XMMS_CMD_FUNC (volume_set));
	xmms_object_cmd_add (XMMS_OBJECT (output),
	                     XMMS_IPC_CMD_VOLUME_GET,
	                     XMMS_CMD_FUNC (volume_get));

	output->status = XMMS_PLAYBACK_STATUS_STOP;

	if (plugin) {
		if (!set_plugin (output, plugin)) {
			xmms_log_error ("Could not initialize output plugin");
		}
	} else {
		xmms_log_error ("initalized output without a plugin, please fix!");
	}



	return output;
}

/**
 * Flush the buffers in soundcard.
 */
void
xmms_output_flush (xmms_output_t *output)
{
	g_return_if_fail (output);

	xmms_output_plugin_method_flush (output->plugin, output);
}

/**
 * @internal
 */
static gboolean
xmms_output_format_set (xmms_output_t *output, xmms_stream_type_t *fmt)
{
	g_return_val_if_fail (output, FALSE);
	g_return_val_if_fail (fmt, FALSE);

	XMMS_DBG ("Setting format!");

	if (!xmms_output_plugin_format_set_always (output->plugin)) {
		gboolean ret;

		if (output->format && xmms_stream_type_match (output->format, fmt)) {
			XMMS_DBG ("audio formats are equal, not updating");
			return TRUE;
		}

		ret = xmms_output_plugin_method_format_set (output->plugin, output, fmt);
		if (ret) {
			xmms_object_unref (output->format);
			xmms_object_ref (fmt);
			output->format = fmt;
		}
		return ret;
	} else {
		if (output->format && !xmms_stream_type_match (output->format, fmt)) {
			xmms_object_unref (output->format);
			xmms_object_ref (fmt);
			output->format = fmt;
		}
		if (!output->format) {
			xmms_object_unref (output->format);
			xmms_object_ref (fmt);
			output->format = fmt;
		}
		return xmms_output_plugin_method_format_set (output->plugin, output, output->format);
	}
}


static gboolean
set_plugin (xmms_output_t *output, xmms_output_plugin_t *plugin)
{
	gboolean ret;

	g_assert (output);
	g_assert (plugin);

	output->monitor_volume_running = FALSE;
	if (output->monitor_volume_thread) {
		g_thread_join (output->monitor_volume_thread);
		output->monitor_volume_thread = NULL;
	}

	if (output->plugin) {
		xmms_output_plugin_method_destroy (output->plugin, output);
		output->plugin = NULL;
	}
	xmms_output_format_list_clear (output);

	/* output->plugin needs to be set before we can call the
	 * NEW method
	 */
	output->plugin = plugin;
	ret = xmms_output_plugin_method_new (output->plugin, output);

	if (!ret) {
		output->plugin = NULL;
	} else if (!output->monitor_volume_thread) {
		output->monitor_volume_running = TRUE;
		output->monitor_volume_thread = g_thread_create (xmms_output_monitor_volume_thread,
		                                                 output, TRUE, NULL);
	}

	return ret;
}

static gint
xmms_volume_map_lookup (xmms_volume_map_t *vl, const gchar *name)
{
	gint i;

	for (i = 0; i < vl->num_channels; i++) {
		if (!strcmp (vl->names[i], name)) {
			return i;
		}
	}

	return -1;
}

/* returns TRUE when both hashes are equal, else FALSE */
static gboolean
xmms_volume_map_equal (xmms_volume_map_t *a, xmms_volume_map_t *b)
{
	guint i;

	g_assert (a);
	g_assert (b);

	if (a->num_channels != b->num_channels) {
		return FALSE;
	}

	for (i = 0; i < a->num_channels; i++) {
		gint j;

		j = xmms_volume_map_lookup (b, a->names[i]);
		if (j == -1 || b->values[j] != a->values[i]) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
xmms_volume_map_init (xmms_volume_map_t *vl)
{
	vl->status = FALSE;
	vl->num_channels = 0;
	vl->names = NULL;
	vl->values = NULL;
}

static void
xmms_volume_map_free (xmms_volume_map_t *vl)
{
	g_free (vl->names);
	g_free (vl->values);

	/* don't free vl here, its always allocated on the stack */
}

static void
xmms_volume_map_copy (xmms_volume_map_t *src, xmms_volume_map_t *dst)
{
	dst->num_channels = src->num_channels;
	dst->status = src->status;

	if (!src->status) {
		g_free (dst->names);
		dst->names = NULL;

		g_free (dst->values);
		dst->values = NULL;

		return;
	}

	dst->names = g_renew (const gchar *, dst->names, src->num_channels);
	dst->values = g_renew (guint, dst->values, src->num_channels);

	memcpy (dst->names, src->names, src->num_channels * sizeof (gchar *));
	memcpy (dst->values, src->values, src->num_channels * sizeof (guint));
}

static GTree *
xmms_volume_map_to_dict (xmms_volume_map_t *vl)
{
	GTree *ret;
	gint i;

	ret = g_tree_new_full ((GCompareDataFunc) strcmp, NULL,
	                       NULL, (GDestroyNotify) xmmsv_unref);
	if (!ret) {
		return NULL;
	}

	for (i = 0; i < vl->num_channels; i++) {
		xmmsv_t *val;

		val = xmmsv_new_int (vl->values[i]);
		g_tree_replace (ret, (gpointer) vl->names[i], val);
	}

	return ret;
}

static gpointer
xmms_output_monitor_volume_thread (gpointer data)
{
	GTree *dict;
	xmms_output_t *output = data;
	xmms_volume_map_t old, cur;

	if (!xmms_output_plugin_method_volume_get_available (output->plugin)) {
		return NULL;
	}

	xmms_volume_map_init (&old);
	xmms_volume_map_init (&cur);

	while (output->monitor_volume_running) {
		cur.num_channels = 0;
		cur.status = xmms_output_plugin_method_volume_get (output->plugin,
		                                                   output, NULL, NULL,
		                                                   &cur.num_channels);

		if (cur.status) {
			/* check for sane values */
			if (cur.num_channels < 1 ||
			    cur.num_channels > VOLUME_MAX_CHANNELS) {
				cur.status = FALSE;
			} else {
				cur.names = g_renew (const gchar *, cur.names,
				                     cur.num_channels);
				cur.values = g_renew (guint, cur.values, cur.num_channels);
			}
		}

		if (cur.status) {
			cur.status =
				xmms_output_plugin_method_volume_get (output->plugin,
				                                      output, cur.names,
				                                      cur.values,
				                                      &cur.num_channels);
		}

		/* we failed at getting volume for one of the two maps or
		 * we succeeded both times and they differ -> changed
		 */
		if ((cur.status ^ old.status) ||
		    (cur.status && old.status &&
		     !xmms_volume_map_equal (&old, &cur))) {
			/* emit the broadcast */
			if (cur.status) {
				dict = xmms_volume_map_to_dict (&cur);
				xmms_object_emit_f (XMMS_OBJECT (output),
				                    XMMS_IPC_SIGNAL_PLAYBACK_VOLUME_CHANGED,
				                    XMMSV_TYPE_DICT, dict);
				g_tree_destroy (dict);
			} else {
				/** @todo When bug 691 is solved, emit an error here */
				xmms_object_emit_f (XMMS_OBJECT (output),
				                    XMMS_IPC_SIGNAL_PLAYBACK_VOLUME_CHANGED,
				                    XMMSV_TYPE_NONE);
			}
		}

		xmms_volume_map_copy (&cur, &old);

		g_usleep (G_USEC_PER_SEC);
	}

	xmms_volume_map_free (&old);
	xmms_volume_map_free (&cur);

	return NULL;
}

/** @} */
