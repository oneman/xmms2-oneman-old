/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003	Peter Alm, Tobias Rundstr�m, Anders Gustafsson
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




/** @file 
 * Controls transport plugins.
 *
 * This file is responsible for the transportlayer.
 */

#include "xmms/transport.h"
#include "xmms/plugin.h"
#include "xmms/object.h"
#include "xmms/util.h"
#include "xmms/ringbuf.h"
#include "xmms/signal_xmms.h"
#include "xmms/playlist.h"

#include "internal/transport_int.h"
#include "internal/plugin_int.h"

#include <glib.h>
#include <string.h>

#include <sys/stat.h>

/*
 * Static function prototypes
 */

static void xmms_transport_destroy (xmms_object_t *object);
static xmms_plugin_t *xmms_transport_plugin_find (const gchar *url);
static gpointer xmms_transport_thread (gpointer data);

/*
 * Type definitions
 */

/** @defgroup Transport Transport
  * @ingroup XMMSServer
  * @{
  */

/** This describes a directory or a file */
struct xmms_transport_entry_St {
	/** Absolute path to entry */
	gchar path[1024];
	xmms_transport_entry_type_t type;
};

struct xmms_transport_St {
	/** Object for emiting signals */
	xmms_object_t object;
	xmms_plugin_t *plugin; /**< The plugin used as media. */

	/** The entry that are transported.
	 * The url will be extracted from this
	 * upon open */
	xmms_medialib_entry_t entry;

	GMutex *mutex;
	GCond *cond;
	GCond *mime_cond;
	GThread *thread;
	/** This is true if we are currently buffering. */
	gboolean running;

	xmms_ringbuf_t *buffer;
	/** String containing current mimetype */
	gchar *mimetype;
	/** Private plugin data */
	gpointer plugin_data;

	gint numread; /**< times we have read since the last seek / start */
	gboolean buffering;

	/** Number of bytes read from the transport */
	guint64 total_bytes;

	/** Number of buffer underruns */
	guint32 buffer_underruns;

	/** Error status for when we're buffering */
	xmms_error_t status;

	guint64 current_position; 	
};

/*
 * Public functions
 */

/**
 * Returns the plugin for this transport.
 */

xmms_plugin_t *
xmms_transport_plugin_get (const xmms_transport_t *transport)
{
	g_return_val_if_fail (transport, NULL);

	return transport->plugin;
}

/**
  * Get a transport's private data.
  *
  * @returns Pointer to private data.
  */

gpointer
xmms_transport_private_data_get (xmms_transport_t *transport)
{
	gpointer ret;
	g_return_val_if_fail (transport, NULL);

	ret = transport->plugin_data;

	return ret;
}

/**
 * Set a transport's private data
 *
 * @param transport the transport to store the pointer in.
 * @param data pointer to private data.
 */

void
xmms_transport_private_data_set (xmms_transport_t *transport, gpointer data)
{
	g_mutex_lock (transport->mutex);
	transport->plugin_data = data;
	g_mutex_unlock (transport->mutex);
}

/**
 * Wether this transport is a local one.
 * 
 * A local transport is file / socket / fd
 * Remote transports are http / ftp.
 * This is decided by the plugin with the property 
 * XMMS_PLUGIN_PROPERTY_LOCAL
 *
 * @param transport the transport structure.
 * @returns TRUE if this transport plugin has XMMS_PLUGIN_PROPERTY_LOCAL
 */
gboolean
xmms_transport_islocal (xmms_transport_t *transport)
{
	g_return_val_if_fail (transport, FALSE);

	return xmms_plugin_properties_check (transport->plugin, XMMS_PLUGIN_PROPERTY_LOCAL);
}

gboolean
xmms_transport_can_seek (xmms_transport_t *transport)
{
	g_return_val_if_fail (transport, FALSE);

	return xmms_plugin_properties_check (transport->plugin, XMMS_PLUGIN_PROPERTY_SEEK);
}


/**
 * Sets this transports mimetype.
 *
 * This should be called from the plugin to propagate the mimetype
 */
void
xmms_transport_mimetype_set (xmms_transport_t *transport, const gchar *mimetype)
{
	g_return_if_fail (transport);

	g_mutex_lock (transport->mutex);
	
	if (transport->mimetype)
		g_free (transport->mimetype);

	if (mimetype) {
		transport->mimetype = g_strdup (mimetype);
	} else {
		transport->mimetype = NULL;
	}

	g_mutex_unlock (transport->mutex);
	
	if (transport->running)
		xmms_object_emit (XMMS_OBJECT (transport), XMMS_IPC_SIGNAL_TRANSPORT_MIMETYPE, mimetype);

	g_cond_signal (transport->mime_cond);
}

/**
 * Initialize a transport_t from a URI
 *
 * @returns a allocated xmms_transport_t that needs to be freed by a
 * xmms_transport_close ()
 */
xmms_transport_t *
xmms_transport_new ()
{
	xmms_transport_t *transport;
	xmms_config_value_t *val;

	val = xmms_config_lookup ("transport.buffersize");

	transport = xmms_object_new (xmms_transport_t, xmms_transport_destroy);
	transport->mutex = g_mutex_new ();
	transport->cond = g_cond_new ();
	transport->mime_cond = g_cond_new ();
	transport->buffer = xmms_ringbuf_new (xmms_config_value_int_get (val));
	transport->buffering = FALSE; /* maybe should be true? */
	transport->total_bytes = 0;
	transport->buffer_underruns = 0;
	transport->current_position = 0; 
	
	XMMS_DBG ("MEMDBG: TRANSPORT NEW %p", transport);

	return transport;
}

/**
 * Opens the transport.
 *
 * This will call the plugins open function. 
 * Could be a socket opening or a FD or something
 * like that.
 *
 * @param transport the transport structure.
 * @param entry A entry containing a URL that should be
 * 	  used by this transport.
 */

gboolean
xmms_transport_open (xmms_transport_t *transport, xmms_medialib_entry_t entry)
{
	gchar *tmp;
	g_return_val_if_fail (entry, FALSE);
	g_return_val_if_fail (transport, FALSE);

	tmp = xmms_medialib_entry_property_get (entry, XMMS_MEDIALIB_ENTRY_PROPERTY_URL);
	XMMS_DBG ("Trying to open stream: %s", tmp);
	
	transport->plugin = xmms_transport_plugin_find (tmp);
	g_free (tmp);
	if (!transport->plugin)
		return FALSE;

	transport->entry = entry;
	
	XMMS_DBG ("Found plugin: %s", xmms_plugin_name_get (transport->plugin));

	return xmms_transport_plugin_open (transport, entry, NULL);
}

/** 
 * Gets the current URL from the transport.
 */
const gchar *
xmms_transport_url_get (const xmms_transport_t *const transport)
{
	const gchar *ret;
	g_return_val_if_fail (transport, NULL);

	g_mutex_lock (transport->mutex);
	ret =  xmms_medialib_entry_property_get (transport->entry, XMMS_MEDIALIB_ENTRY_PROPERTY_URL);
	g_mutex_unlock (transport->mutex);

	return ret;
}

/** 
 * Gets the current playlist entry from the transport.
 */
xmms_medialib_entry_t
xmms_transport_medialib_entry_get (const xmms_transport_t *const transport)
{
	g_return_val_if_fail (transport, NULL);
	return transport->entry;
}

/**
 * Gets the current mimetype
 * This can return NULL if plugin has not
 * called xmms_transport_mimetype_set()
 */
const gchar *
xmms_transport_mimetype_get (xmms_transport_t *transport)
{
	const gchar *ret;
	g_return_val_if_fail (transport, NULL);

	g_mutex_lock (transport->mutex);
	ret =  transport->mimetype;
	g_mutex_unlock (transport->mutex);

	return ret;
}

/**
 * Like xmms_transport_mimetype_get but blocks if
 * transport plugin has not yet called mimetype_set
 */
const gchar *
xmms_transport_mimetype_get_wait (xmms_transport_t *transport)
{
	const gchar *ret;
	g_return_val_if_fail (transport, NULL);

	g_mutex_lock (transport->mutex);
	if (!transport->mimetype) {
		XMMS_DBG ("Waiting for mime_cond");
		g_cond_wait (transport->mime_cond, transport->mutex);
	}
	ret = transport->mimetype;
	g_mutex_unlock (transport->mutex);

	return ret;
}


/**
 * Tell the transport to start buffer, this is normaly done
 * after you read twice from the buffer
 */
void
xmms_transport_buffering_start (xmms_transport_t *transport)
{
	g_mutex_lock (transport->mutex);
	transport->buffering = TRUE;
	g_cond_signal (transport->cond);
	g_mutex_unlock (transport->mutex);
}

/**
 * Reads len bytes into buffer.
 *
 * This function reads from the transport thread buffer, if you want to
 * read more then currently are buffered, it will wait for you. Does not
 * guarantee that all bytes are read, may return less bytes.
 *
 * @param transport transport to read from.
 * @param buffer where to store read data.
 * @param len number of bytes to read.
 * @returns number of bytes actually read, or -1 on error.
 *
 */
gint
xmms_transport_read (xmms_transport_t *transport, gchar *buffer, guint len, xmms_error_t *error)
{
	gint ret;

	g_return_val_if_fail (transport, -1);
	g_return_val_if_fail (buffer, -1);
	g_return_val_if_fail (len > 0, -1);
	g_return_val_if_fail (error, -1);

	g_mutex_lock (transport->mutex);

	/* Unbuffered read */

	if (!transport->buffering) {
		xmms_transport_read_method_t read_method;

		XMMS_DBG ("Doing unbuffered read...");

		read_method = xmms_plugin_method_get (transport->plugin, XMMS_PLUGIN_METHOD_READ);
		if (!read_method) {
			g_mutex_unlock (transport->mutex);
			return -1;
		}

		g_mutex_unlock (transport->mutex);
		ret = read_method (transport, buffer, len, error);
		g_mutex_lock (transport->mutex);

		if (ret <= 0) {
			xmms_error_set (&transport->status, error->code, error->message);
		}

		if (ret != -1) {
			transport->current_position += ret;
		}

		if (transport->running && !transport->buffering && transport->numread++ > 1) {
			XMMS_DBG ("Let's start buffering");
			transport->buffering = TRUE;
			g_cond_signal (transport->cond);
		}


		g_mutex_unlock (transport->mutex);
		return ret;
	}

	/* Buffered read */

	if (len > xmms_ringbuf_size (transport->buffer)) {
		len = xmms_ringbuf_size (transport->buffer);;
	}

	if (xmms_ringbuf_iseos (transport->buffer)) {
		gint val = -1;

		xmms_error_set (error, transport->status.code, transport->status.message);
		if (transport->status.code == XMMS_ERROR_EOS) {
			val = 0;
		}

		g_mutex_unlock (transport->mutex);
		return val;
	}

	ret = xmms_ringbuf_read_wait (transport->buffer, buffer, len, transport->mutex);

	if (ret < len) {
		transport->buffer_underruns ++;
	}

	transport->total_bytes += ret;
	transport->current_position += ret; 

	g_mutex_unlock (transport->mutex);

	return ret;
}

/**
 * 
 * Seek to a specific offset in a transport. Emulates the behaviour of
 * lseek. Buffering is disabled after a seek (automatically enabled
 * after two reads).
 *
 * The whence parameter should be one of:
 * @li @c XMMS_TRANSPORT_SEEK_SET Sets position to offset from start of file
 * @li @c XMMS_TRANSPORT_SEEK_END Sets position to offset from end of file
 * @li @c XMMS_TRANSPORT_SEEK_CUR Sets position to offset from current position
 *
 * @param transport the transport to modify
 * @param offset offset in bytes
 * @param whence se above
 * @returns new position, or -1 on error
 * 
 */
gint
xmms_transport_seek (xmms_transport_t *transport, gint offset, gint whence)
{
	xmms_transport_seek_method_t seek_method;
	gboolean ret;

	g_return_val_if_fail (transport, FALSE);

	g_mutex_lock (transport->mutex);

	if (!xmms_plugin_properties_check (transport->plugin, XMMS_PLUGIN_PROPERTY_SEEK)) {
		g_mutex_unlock (transport->mutex);
		return -1;
	}
	
	seek_method = xmms_plugin_method_get (transport->plugin, XMMS_PLUGIN_METHOD_SEEK);
	if (!seek_method) {
		XMMS_DBG ("This plugin has XMMS_PLUGIN_PROPERTY_SEEK but no seek method, that's stupid");
		g_mutex_unlock (transport->mutex);
		return -1;
	}

	/* reset the buffer */
	transport->buffering = FALSE;
	transport->numread = 0;
	xmms_ringbuf_clear (transport->buffer);
	xmms_ringbuf_set_eos (transport->buffer, FALSE);

	if (whence == XMMS_TRANSPORT_SEEK_CUR) {
		whence = XMMS_TRANSPORT_SEEK_SET;
		offset = transport->current_position + offset;
	}

	XMMS_DBG ("Seeking to %d", offset);

	ret = seek_method (transport, offset, whence);

	XMMS_DBG ("Seek method returned %d", ret);

	if (ret != -1)
		transport->current_position = ret; 

	g_mutex_unlock (transport->mutex);

	return ret;
}

/**
  * Obtain the current value of the stream position indicator for transport
  * 
  * @returns current position in stream 
  */
gint
xmms_transport_tell (xmms_transport_t *transport)
{
	g_return_val_if_fail (transport, -1); 

	return transport->current_position; 
}

gboolean
xmms_transport_iseos (xmms_transport_t *transport)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (transport, FALSE);

	g_mutex_lock (transport->mutex);

	if (transport->buffering) {
		ret = xmms_ringbuf_iseos (transport->buffer);
	} else if (xmms_error_iserror (&transport->status)) {
		ret = TRUE;
	}

	g_mutex_unlock (transport->mutex);
	return ret;
}

/**
  * Gets the total size of the transports media.
  *
  * @returns size of the media, or -1 if it can't be determined.
  */
gint
xmms_transport_size (xmms_transport_t *transport)
{
	xmms_transport_size_method_t size;
	g_return_val_if_fail (transport, -1);

	size = xmms_plugin_method_get (transport->plugin, XMMS_PLUGIN_METHOD_SIZE);
	g_return_val_if_fail (size, -1);

	return size (transport);
}

/**
 * Gets the plugin that was used to instantiate this transport
 *
 */
xmms_plugin_t *
xmms_transport_get_plugin (const xmms_transport_t *transport)
{
	g_return_val_if_fail (transport, NULL);

	return transport->plugin;

}

/**
 * Instantiate a transport plugin.
 *
 */

gboolean
xmms_transport_plugin_open (xmms_transport_t *transport, xmms_medialib_entry_t entry, 
		gpointer data)
{
	xmms_transport_open_method_t init_method;
	xmms_transport_lmod_method_t lmod_method;
	xmms_plugin_t *plugin;
	gchar *url;
	
	plugin = transport->plugin;
	
	init_method = xmms_plugin_method_get (plugin, XMMS_PLUGIN_METHOD_INIT);
	lmod_method = xmms_plugin_method_get (plugin, XMMS_PLUGIN_METHOD_LMOD);

	if (!init_method) {
		XMMS_DBG ("Transport has no init method!");
		return FALSE;
	}

	xmms_transport_private_data_set (transport, data);

	url = xmms_medialib_entry_property_get (transport->entry, XMMS_MEDIALIB_ENTRY_PROPERTY_URL);

	if (!init_method (transport, url)) {
		g_free (url);
		return FALSE;
	}

	g_free (url);

	if (lmod_method) {
		guint lmod;
		gchar *lmod_str;
		lmod = lmod_method (transport);
		lmod_str = g_strdup_printf ("%d", lmod);
		xmms_medialib_entry_property_set (transport->entry, XMMS_MEDIALIB_ENTRY_PROPERTY_LMOD, lmod_str);
		g_free (lmod_str);
	}


	return TRUE;
}


/*
 * Private functions
 */

/**
  * Start the transport thread.
  * This should be called to make the transport start buffer.
  *
  * @internal
  */

void
xmms_transport_start (xmms_transport_t *transport)
{
	g_return_if_fail (transport);

	transport->running = TRUE;
	transport->thread = g_thread_create (xmms_transport_thread, transport, TRUE, NULL); 
}


/**
  * Tells the transport thread to quit and call xmms_transport_destroy
  *
  * @internal
  */

void
xmms_transport_close (xmms_transport_t *transport)
{
	g_return_if_fail (transport);

	if (transport->thread) {
		g_mutex_lock (transport->mutex);
		transport->running = FALSE;
		xmms_ringbuf_set_eos (transport->buffer, TRUE);
		XMMS_DBG("Waking transport");
		g_cond_signal (transport->cond);
		g_mutex_unlock (transport->mutex);
		g_thread_join (transport->thread);
	}
}

/*
 * Static functions
 */
static void
xmms_transport_destroy (xmms_object_t *object)
{
	xmms_transport_t *transport = (xmms_transport_t *)object;
	xmms_transport_close_method_t close_method;

	close_method = xmms_plugin_method_get (transport->plugin, XMMS_PLUGIN_METHOD_CLOSE);
	
	if (close_method)
		close_method (transport);

	xmms_object_unref (transport->plugin);

	xmms_ringbuf_destroy (transport->buffer);
	g_cond_free (transport->mime_cond);
	g_cond_free (transport->cond);
	g_mutex_free (transport->mutex);
	
	XMMS_DBG ("MEMDBG: TRANSPORT DESTROY %p", object);

	if (transport->mimetype)
		g_free (transport->mimetype);
}

/**
 * Finds a transportplugin for this URL.
 *
 * @internal
 */

static xmms_plugin_t *
xmms_transport_plugin_find (const gchar *url)
{
	GList *list, *node;
	xmms_plugin_t *plugin = NULL;
	xmms_transport_can_handle_method_t can_handle;

	g_return_val_if_fail (url, NULL);

	list = xmms_plugin_list_get (XMMS_PLUGIN_TYPE_TRANSPORT);
	
	for (node = list; node; node = g_list_next (node)) {
		plugin = node->data;
		XMMS_DBG ("Trying plugin: %s", xmms_plugin_name_get (plugin));
		can_handle = xmms_plugin_method_get (plugin, XMMS_PLUGIN_METHOD_CAN_HANDLE);
		
		if (!can_handle)
			continue;

		if (can_handle (url)) {
			xmms_object_ref (plugin);
			break;
		}
	}
	if (!node)
		plugin = NULL;

	if (list)
		xmms_plugin_list_destroy (list);

	return plugin;
}

static gpointer
xmms_transport_thread (gpointer data)
{
	xmms_transport_t *transport = data;
	gchar buffer[4096];
	xmms_error_t error;
	xmms_transport_read_method_t read_method;
	gint ret;

	g_return_val_if_fail (transport, NULL);
	
	read_method = xmms_plugin_method_get (transport->plugin, XMMS_PLUGIN_METHOD_READ);
	if (!read_method)
		return NULL;

	xmms_error_reset (&error);

	xmms_object_ref (transport);
	g_mutex_lock (transport->mutex);
	while (transport->running) {

		if (!transport->buffering) {
			XMMS_DBG ("Holding pattern");
			g_cond_wait (transport->cond, transport->mutex);
		}

		if (!transport->buffering)
			continue;

		g_mutex_unlock (transport->mutex);
		ret = read_method (transport, buffer, sizeof(buffer), &error);
		g_mutex_lock (transport->mutex);

		if (ret > 0) {
			xmms_ringbuf_write_wait (transport->buffer, buffer, ret, transport->mutex);
		} else {
			xmms_ringbuf_set_eos (transport->buffer, TRUE);
			xmms_error_set (&transport->status, error.code, error.message);
			XMMS_DBG("Sleeping on transport cond");
			g_cond_wait (transport->cond, transport->mutex);
		}
	}
	g_mutex_unlock (transport->mutex);

	XMMS_DBG ("xmms_transport_thread: cleaning up");
	
	xmms_object_unref (transport);
	
	return NULL;
}

/** @} */
