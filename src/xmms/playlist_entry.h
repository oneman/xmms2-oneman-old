#ifndef __XMMS_PLAYLIST_ENTRY_H__
#define __XMMS_PLAYLIST_ENTRY_H__

#include <glib.h>

#include "xmms/playlist.h"

/*
 * Type definitions
 */

struct xmms_playlist_entry_St;
typedef struct xmms_playlist_entry_St xmms_playlist_entry_t;



#define XMMS_PLAYLIST_ENTRY_PROPERTY_ARTIST "artist"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_ALBUM "album"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_TITLE "title"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_YEAR "date"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_TRACKNR "tracknr"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_GENRE "genre"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_BITRATE "bitrate"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_COMMENT "comment"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_DURATION "duration"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_CHANNEL "channel"
#define XMMS_PLAYLIST_ENTRY_PROPERTY_SAMPLERATE "samplerate"


/*
 * Public API
 */

xmms_playlist_entry_t * xmms_playlist_entry_new (gchar *url);

void xmms_playlist_entry_property_foreach (xmms_playlist_entry_t *entry,
					   GHFunc func,
					   gpointer userdata);
void xmms_playlist_entry_property_set (xmms_playlist_entry_t *entry, gchar *key, 
			    	   gchar *value);
void xmms_playlist_entry_url_set (xmms_playlist_entry_t *entry, gchar *url);
void xmms_playlist_entry_mimetype_set (xmms_playlist_entry_t *entry, 
				       const gchar *mimetype);
const gchar *xmms_playlist_entry_mimetype_get (xmms_playlist_entry_t *entry);
gchar *xmms_playlist_entry_url_get (const xmms_playlist_entry_t *entry);
guint xmms_playlist_entry_id_get (xmms_playlist_entry_t *entry);
void xmms_playlist_entry_id_set (xmms_playlist_entry_t *entry, guint id);
gchar *xmms_playlist_entry_property_get (const xmms_playlist_entry_t *entry, 
					 gchar *key);
gint xmms_playlist_entry_property_get_int (const xmms_playlist_entry_t *entry, 
					   gchar *key);
void xmms_playlist_entry_property_copy (xmms_playlist_entry_t *entry, 
					xmms_playlist_entry_t *newentry);
gboolean xmms_playlist_entry_iswellknown (gchar *property);
void xmms_playlist_entry_ref (xmms_playlist_entry_t *entry);
void xmms_playlist_entry_unref (xmms_playlist_entry_t *entry);

#endif
