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

#include "xmms/xmms_log.h"

#ifndef RINGBUFTEST
#include "xmmspriv/xmms_ringbuf.h"
#endif

#include <string.h>

/** @defgroup Ringbuffer Ringbuffer
  * @ingroup XMMSServer
  * @brief Ringbuffer primitive.
  * @{
  */

/**
 * A ringbuffer
 */
struct xmms_ringbuf_St {
	/** The actual bufferdata */
	guint8 *buffer;
	/** Number of bytes in #buffer */
	guint buffer_size;
	/** Actually usable number of bytes */
	guint buffer_size_usable;
	/** Read and write index */
	volatile guint rd_index, wr_index;
	gboolean eos;
	gboolean eor;
	GQueue *hotspots;

	GCond *free_cond, *used_cond, *eos_cond;
};

typedef struct xmms_ringbuf_hotspot_St {
	guint pos;
	gboolean (*callback) (void *);
	void (*destroy) (void *);
	void *arg;
} xmms_ringbuf_hotspot_t;



/**
 * The usable size of the ringbuffer.
 */
guint
xmms_ringbuf_size (xmms_ringbuf_t *ringbuf)
{
	g_return_val_if_fail (ringbuf, 0);

	return ringbuf->buffer_size_usable;
}

/**
 * Allocate a new ringbuffer
 *
 * @param size The total size of the new ringbuffer
 * @returns a new #xmms_ringbuf_t
 */
xmms_ringbuf_t *
xmms_ringbuf_new (guint size)
{
	xmms_ringbuf_t *ringbuf = g_new0 (xmms_ringbuf_t, 1);

	g_return_val_if_fail (size > 0, NULL);
	g_return_val_if_fail (size < G_MAXUINT, NULL);

	/* we need to allocate one byte more than requested, cause the
	 * final byte cannot be used.
	 * if we used it, it might lead to the situation where
	 * read_index == write_index, which is used for the "empty"
	 * condition.
	 */
	ringbuf->buffer_size_usable = size;
	ringbuf->buffer_size = size + 1;
	ringbuf->buffer = g_malloc (ringbuf->buffer_size);

	ringbuf->free_cond = g_cond_new ();
	ringbuf->used_cond = g_cond_new ();
	ringbuf->eos_cond = g_cond_new ();

	ringbuf->hotspots = g_queue_new ();

	return ringbuf;
}

/**
 * Free all memory used by the ringbuffer
 */
void
xmms_ringbuf_destroy (xmms_ringbuf_t *ringbuf)
{
	g_return_if_fail (ringbuf);

	g_cond_free (ringbuf->eos_cond);
	g_cond_free (ringbuf->used_cond);
	g_cond_free (ringbuf->free_cond);

	g_queue_free (ringbuf->hotspots);
	g_free (ringbuf->buffer);
	g_free (ringbuf);
}

/**
 * Clear the ringbuffers data
 */
void
xmms_ringbuf_clear (xmms_ringbuf_t *ringbuf)
{
	g_return_if_fail (ringbuf);

	ringbuf->rd_index = 0;
	ringbuf->wr_index = 0;

	while (!g_queue_is_empty (ringbuf->hotspots)) {
		xmms_ringbuf_hotspot_t *hs;
		hs = g_queue_pop_head (ringbuf->hotspots);
		if (hs->destroy)
			hs->destroy (hs->arg);
		g_free (hs);
	}
	g_cond_signal (ringbuf->free_cond);
}

/**
 * Number of bytes free in the ringbuffer
 */
guint
xmms_ringbuf_bytes_free (const xmms_ringbuf_t *ringbuf)
{
	g_return_val_if_fail (ringbuf, 0);

	return ringbuf->buffer_size_usable -
	       xmms_ringbuf_bytes_used (ringbuf);
}

/**
 * Number of bytes used in the buffer
 */
guint
xmms_ringbuf_bytes_used (const xmms_ringbuf_t *ringbuf)
{
	g_return_val_if_fail (ringbuf, 0);

	guint wr_index_tmp, rd_index_tmp;

	wr_index_tmp = ringbuf->wr_index;
	rd_index_tmp = ringbuf->rd_index;		

	if (wr_index_tmp >= rd_index_tmp) {
		return wr_index_tmp - rd_index_tmp;
	}

	return ringbuf->buffer_size - (rd_index_tmp - wr_index_tmp);
}


gint xmms_ringbuf_get_next_hotspot_pos (xmms_ringbuf_t *ringbuf) {

	while (!g_queue_is_empty (ringbuf->hotspots)) {
		xmms_ringbuf_hotspot_t *hs = g_queue_peek_head (ringbuf->hotspots);
		return hs->pos;
	}


	return -1;

}

void xmms_ringbuf_hit_hotspot (xmms_ringbuf_t *ringbuf) {

	gboolean ok;

		xmms_ringbuf_hotspot_t *hs = g_queue_peek_head (ringbuf->hotspots);
		(void) g_queue_pop_head (ringbuf->hotspots);
		ok = hs->callback (hs->arg);
		if (hs->destroy)
			hs->destroy (hs->arg);
		g_free (hs);

		if (!ok) {
			//return 0;
		}
}

gint
xmms_ringbuf_get_read_pos (xmms_ringbuf_t *ringbuf)
{
	return ringbuf->rd_index;
}

static guint
read_bytes (xmms_ringbuf_t *ringbuf, guint8 *data, guint len)
{
	guint to_read, bytes_used, r = 0, cnt, tmp;
	gboolean ok;


	bytes_used = xmms_ringbuf_bytes_used (ringbuf);
	to_read = MIN (len, bytes_used);

	while (!g_queue_is_empty (ringbuf->hotspots)) {
		xmms_ringbuf_hotspot_t *hs = g_queue_peek_head (ringbuf->hotspots);
		if (hs->pos != ringbuf->rd_index) {
			/* make sure we don't cross a hotspot */
			to_read = MIN (to_read,
			               (hs->pos - ringbuf->rd_index + ringbuf->buffer_size)
			               % ringbuf->buffer_size);
			break;
		}

		(void) g_queue_pop_head (ringbuf->hotspots);
		ok = hs->callback (hs->arg);
		if (hs->destroy)
			hs->destroy (hs->arg);
		g_free (hs);

		if (!ok) {
			return 0;
		}

		/* we loop here, to see if there are multiple
		   hotspots in same position */
	}

	tmp = ringbuf->rd_index;

	while (to_read > 0) {
		cnt = MIN (to_read, ringbuf->buffer_size - tmp);
		memcpy (data, ringbuf->buffer + tmp, cnt);
		tmp = (tmp + cnt) % ringbuf->buffer_size;
		to_read -= cnt;
		r += cnt;
		data += cnt;
	}

	return r;
}

/**
 * Reads data from the ringbuffer. This is a non-blocking call and can
 * return less data than you wanted. Use #xmms_ringbuf_wait_used to
 * ensure that you get as much data as you want.
 *
 * @param ringbuf Buffer to read from
 * @param data Allocated buffer where the readed data will end up
 * @param len number of bytes to read
 * @returns number of bytes that acutally was read.
 */
guint
xmms_ringbuf_read (xmms_ringbuf_t *ringbuf, gpointer data, guint len)
{
	guint r;

	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);

	r = read_bytes (ringbuf, (guint8 *) data, len);

	ringbuf->rd_index = (ringbuf->rd_index + r) % ringbuf->buffer_size;

	if (r) {
		g_cond_broadcast (ringbuf->free_cond);
	}

	return r;
}



/**
 * Reads data from the ringbuffer. This is a non-blocking call and can
 * return less data than you wanted. Use #xmms_ringbuf_wait_used to
 * ensure that you get as much data as you want.
 *
 * @param ringbuf Buffer to read from
 * @param data Allocated buffer where the readed data will end up
 * @param len number of bytes to read
 * @returns number of bytes that acutally was read.
 */
guint
xmms_ringbuf_read_cutzero (xmms_ringbuf_t *ringbuf, gpointer data, guint len)
{
	guint r;

	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);

	r = read_bytes (ringbuf, (guint8 *) data, len);
	
	float *data2;
	data2 = data;
	int x, r2,y ;
	r2 = r;
	x = find_final_zero_crossing (data, r);
	if(x != -1) {
	
		r2 = x * 8;

		for(y = (x * 2 ) - 2; y < len / 4; y++) {
			data2[y] = 0.0; 
		}

	}

	ringbuf->rd_index = (ringbuf->rd_index + r2) % ringbuf->buffer_size;

	if (r) {
		g_cond_broadcast (ringbuf->free_cond);
	}

	return r;
}


/**
 * Same as #xmms_ringbuf_read but does not advance in the buffer after
 * the data has been read.
 *
 * @sa xmms_ringbuf_read
 */
guint
xmms_ringbuf_peek (xmms_ringbuf_t *ringbuf, gpointer data, guint len)
{
	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);
	g_return_val_if_fail (len <= ringbuf->buffer_size_usable, 0);

	return read_bytes (ringbuf, (guint8 *) data, len);
}

/**
 * Same as #xmms_ringbuf_read but blocks until you have all the data you want.
 *
 * @sa xmms_ringbuf_read
 */
guint
xmms_ringbuf_read_wait (xmms_ringbuf_t *ringbuf, gpointer data,
                        guint len, GMutex *mtx)
{
	guint r = 0, res;
	guint8 *dest = data;

	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);
	g_return_val_if_fail (mtx, 0);

	while (r < len) {
		res = xmms_ringbuf_read (ringbuf, dest + r, len - r);
		r += res;
		if (r == len || ringbuf->eos) {
			break;
		}
		if (!res)
			g_cond_wait (ringbuf->used_cond, mtx);
	}

	return r;
}

/**
 * Same as #xmms_ringbuf_peek but blocks until you have all the data you want.
 *
 * @sa xmms_ringbuf_peek
 */
guint
xmms_ringbuf_peek_wait (xmms_ringbuf_t *ringbuf, gpointer data,
                        guint len, GMutex *mtx)
{
	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);
	g_return_val_if_fail (len <= ringbuf->buffer_size_usable, 0);
	g_return_val_if_fail (mtx, 0);

	xmms_ringbuf_wait_used (ringbuf, len, mtx);

	return xmms_ringbuf_peek (ringbuf, data, len);
}

/**
 * Write data to the ringbuffer. If not all data can be written
 * to the buffer the function will not block.
 *
 * @sa xmms_ringbuf_write_wait
 *
 * @param ringbuf Ringbuffer to put data in.
 * @param data Data to put in ringbuffer
 * @param len Length of data
 * @returns Number of bytes that was written
 */
guint
xmms_ringbuf_write (xmms_ringbuf_t *ringbuf, gconstpointer data,
                    guint len)
{
	guint to_write, bytes_free, w = 0, cnt;
	const guint8 *src = data;

	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);

	bytes_free = xmms_ringbuf_bytes_free (ringbuf);
	to_write = MIN (len, bytes_free);

	while (to_write > 0) {
		cnt = MIN (to_write, ringbuf->buffer_size - ringbuf->wr_index);
		memcpy (ringbuf->buffer + ringbuf->wr_index, src + w, cnt);
		ringbuf->wr_index = (ringbuf->wr_index + cnt) % ringbuf->buffer_size;
		to_write -= cnt;
		w += cnt;
	}

	if (w) {
		g_cond_broadcast (ringbuf->used_cond);
	}

	return w;
}

/**
 * Same as #xmms_ringbuf_write but blocks until there is enough free space.
 */

guint
xmms_ringbuf_write_wait (xmms_ringbuf_t *ringbuf, gconstpointer data,
                         guint len, GMutex *mtx)
{
	guint w = 0;
	const guint8 *src = data;

	g_return_val_if_fail (ringbuf, 0);
	g_return_val_if_fail (data, 0);
	g_return_val_if_fail (len > 0, 0);
	g_return_val_if_fail (mtx, 0);

	while (w < len) {
		w += xmms_ringbuf_write (ringbuf, src + w, len - w);
		if (w == len || ringbuf->eor) {
			break;
		}

		g_cond_wait (ringbuf->free_cond, mtx);
	}

	return w;
}

/**
 * Block until we have free space in the ringbuffer.
 */
void
xmms_ringbuf_wait_free (const xmms_ringbuf_t *ringbuf, guint len, GMutex *mtx)
{
	g_return_if_fail (ringbuf);
	g_return_if_fail (len > 0);
	g_return_if_fail (len <= ringbuf->buffer_size_usable);
	g_return_if_fail (mtx);

	GTimeVal wait_time;

	while ((xmms_ringbuf_bytes_free (ringbuf) < len) && !ringbuf->eor) {
		g_get_current_time (&wait_time);
		g_time_val_add (&wait_time, 30000);
		g_cond_timed_wait (ringbuf->free_cond, mtx, &wait_time);
	}
}

/**
 * Block until we have used space in the buffer
 */

void
xmms_ringbuf_wait_used (const xmms_ringbuf_t *ringbuf, guint len, GMutex *mtx)
{
	g_return_if_fail (ringbuf);
	g_return_if_fail (len > 0);
	g_return_if_fail (len <= ringbuf->buffer_size_usable);
	g_return_if_fail (mtx);

	GTimeVal wait_time;

	while ((xmms_ringbuf_bytes_used (ringbuf) < len) && !ringbuf->eos) {
		g_get_current_time (&wait_time);
		g_time_val_add (&wait_time, 30000);
		g_cond_timed_wait (ringbuf->used_cond, mtx, &wait_time);
	}
}


/**
 * Block until we have used space in the buffer
 */

gboolean
xmms_ringbuf_timed_wait_used (const xmms_ringbuf_t *ringbuf, guint len, GMutex *mtx, guint ms)
{
	g_return_val_if_fail (ringbuf, FALSE);
	g_return_val_if_fail (len > 0, FALSE);
	g_return_val_if_fail (len <= ringbuf->buffer_size_usable, FALSE);
	g_return_val_if_fail (mtx, FALSE);

	GTimeVal wait_time;

	if ((xmms_ringbuf_bytes_used (ringbuf) < len) && !ringbuf->eos) {
		g_get_current_time (&wait_time);
		g_time_val_add (&wait_time, (ms * 1000));
		return g_cond_timed_wait (ringbuf->used_cond, mtx, &wait_time);
	}
	
	return TRUE;
}

/**
 * Tell if the ringbuffer is EOS
 *
 * @returns TRUE if the ringbuffer is EOSed.
 */

gboolean
xmms_ringbuf_iseos (const xmms_ringbuf_t *ringbuf)
{
	g_return_val_if_fail (ringbuf, TRUE);

	return !xmms_ringbuf_bytes_used (ringbuf) && ringbuf->eos;
}

/**
 * Set EOS flag on ringbuffer.
 */
void
xmms_ringbuf_set_eos (xmms_ringbuf_t *ringbuf, gboolean eos)
{
	g_return_if_fail (ringbuf);

	ringbuf->eos = eos;

	if (eos) {
		g_cond_broadcast (ringbuf->eos_cond);
		g_cond_broadcast (ringbuf->used_cond);
		g_cond_broadcast (ringbuf->free_cond);
	}
}

/**
 * Tell if the ringbuffer is done being read
 *
 * @returns TRUE if the ringbuffer is EORed.
 */

gboolean
xmms_ringbuf_is_eor (const xmms_ringbuf_t *ringbuf)
{
	g_return_val_if_fail (ringbuf, TRUE);

	return ringbuf->eor;
}

/**
 * Set EOR flag on ringbuffer.
 */
void
xmms_ringbuf_set_eor (xmms_ringbuf_t *ringbuf, gboolean eor)
{
	g_return_if_fail (ringbuf);

	ringbuf->eor = eor;

	if (eor) {
		g_cond_broadcast (ringbuf->free_cond);
	}
}


/**
 * Block until we are EOSed
 */
void
xmms_ringbuf_wait_eos (const xmms_ringbuf_t *ringbuf, GMutex *mtx)
{
	g_return_if_fail (ringbuf);
	g_return_if_fail (mtx);

	while (!xmms_ringbuf_iseos (ringbuf)) {
		g_cond_wait (ringbuf->eos_cond, mtx);
	}

}
/** @} */

/**
 * @internal
 * Unused
 */
void
xmms_ringbuf_hotspot_set (xmms_ringbuf_t *ringbuf, gboolean (*cb) (void *), void (*destroy) (void *), void *arg)
{
	xmms_ringbuf_hotspot_t *hs;
	g_return_if_fail (ringbuf);

	hs = g_new0 (xmms_ringbuf_hotspot_t, 1);
	hs->pos = ringbuf->wr_index;
	hs->callback = cb;
	hs->destroy = destroy;
	hs->arg = arg;

	g_queue_push_tail (ringbuf->hotspots, hs);
}


/* Advance the read pointer `cnt' places. */

void
xmms_ringbuf_read_advance (xmms_ringbuf_t * ringbuf, gint cnt)
{
	ringbuf->rd_index = (ringbuf->rd_index + cnt) % ringbuf->buffer_size;
	g_cond_broadcast (ringbuf->free_cond);
}

/* Advance the write pointer `cnt' places. */

void
xmms_ringbuf_write_advance (xmms_ringbuf_t * ringbuf, gint cnt)
{
	ringbuf->wr_index = (ringbuf->wr_index + cnt) % ringbuf->buffer_size;
	g_cond_broadcast (ringbuf->used_cond);
}

/* The non-copying data reader.  `vec' is an array of two places.  Set
   the values at `vec' to hold the current readable data at `rb'.  If
   the readable data is in one segment the second segment has zero
   length.  */

void
xmms_ringbuf_get_read_vector (const xmms_ringbuf_t * rb,
				 xmms_ringbuf_vector_t * vec)
{
	gint used_cnt;
	gint cnt2;
	gint w, r;

	w = rb->wr_index;
	r = rb->rd_index;
/*
	if (w > r) {
		free_cnt = w - r;
	} else {
		free_cnt = (w - r + rb->size) & rb->size_mask;
	}
*/
	used_cnt = xmms_ringbuf_bytes_used(rb);// 6000

	cnt2 = r + used_cnt;  //30000 + 6000
           //36000    32000
	if (cnt2 > rb->buffer_size) {

		/* Two part vector: the rest of the buffer after the current write
		   ptr, plus some from the start of the buffer. */

		vec[0].buf = rb->buffer + r;
		vec[0].len = rb->buffer_size - r; //32000 - 30000  = 2000
		vec[1].buf = rb->buffer;// 0
		vec[1].len = cnt2 - rb->buffer_size; // 36000 - 32000 = 4000

	} else {

		/* Single part vector: just the rest of the buffer */

		vec[0].buf = rb->buffer + r;
		vec[0].len = used_cnt;
		vec[1].len = 0;
	}
}

/* The non-copying data writer.  `vec' is an array of two places.  Set
   the values at `vec' to hold the current writeable data at `rb'.  If
   the writeable data is in one segment the second segment has zero
   length.  */

void
xmms_ringbuf_get_write_vector (const xmms_ringbuf_t * rb,
				  xmms_ringbuf_vector_t * vec)
{
	gint free_cnt;
	gint cnt2;
	gint w, r;

	w = rb->wr_index;
	r = rb->rd_index;
/*
	if (w > r) {
		free_cnt = ((r - w + rb->size) & rb->size_mask) - 1;
	} else if (w < r) {
		free_cnt = (r - w) - 1;
	} else {
		free_cnt = rb->size - 1;
	}
*/
	free_cnt = xmms_ringbuf_bytes_free(rb);

	cnt2 = w + free_cnt;

	if (cnt2 > rb->buffer_size) {

		/* Two part vector: the rest of the buffer after the current write
		   ptr, plus some from the start of the buffer. */

		vec[0].buf = rb->buffer + w;
		vec[0].len = rb->buffer_size - w;
		//vec[1].buf = rb->buffer;
		//vec[1].len = cnt2 - rb->buffer_size;
	} else {
		vec[0].buf = rb->buffer + w;
		vec[0].len = free_cnt;
		//vec[1].len = 0;
	}
}

