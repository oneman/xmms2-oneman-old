/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2007 XMMS2 Team
 *
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 */

#include "utils.h"
#include "status.h"

#include "cli_infos.h"
#include "cli_cache.h"
#include "column_display.h"

static void coll_int_attribute_set (xmmsv_coll_t *coll, const char *key, gint value);
static xmmsv_coll_t *coll_make_reference (const char *name, xmmsc_coll_namespace_t ns);
static void coll_copy_attributes (const char *key, xmmsv_t *val, void *udata);
static void coll_print_attributes (const char *key, xmmsv_t *val, void *udata);
static xmmsv_coll_t *coll_copy_retype (xmmsv_coll_t *coll, xmmsv_coll_type_t type);

static void pl_print_config (xmmsv_coll_t *coll, const char *name);

static void id_print_info (xmmsc_result_t *res, guint id, gchar *source);

static gint compare_uint (gconstpointer a, gconstpointer b, gpointer userdata);

static void coll_dump (xmmsv_coll_t *coll, guint level);

typedef enum {
	IDLIST_CMD_NONE = 0,
	IDLIST_CMD_REHASH,
	IDLIST_CMD_REMOVE
} idlist_command_t;

typedef struct {
	cli_infos_t *infos;
	column_display_t *coldisp;
	const gchar *playlist;
	GArray *entries;
	gint inc;
	gint pos;
} pl_pos_udata_t;

/* Dumps a propdict on stdout */
static void
dict_dump (const gchar *source, xmmsv_t *val, void *udata)
{
	xmmsv_type_t type;

	const gchar **keyfilter = (const gchar **) udata;
	const gchar *key = (const gchar *) keyfilter[0];
	const gchar *filter = (const gchar *) keyfilter[1];

	if (filter && strcmp (filter, source) != 0) {
		return;
	}

	type = xmmsv_get_type (val);

	switch (type) {
	case XMMSV_TYPE_INT32:
	{
		gint value;
		xmmsv_get_int (val, &value);
		g_printf (_("[%s] %s = %u\n"), source, key, value);
		break;
	}
	case XMMSV_TYPE_STRING:
	{
		const gchar *value;
		xmmsv_get_string (val, &value);
		/* FIXME: special handling for url, guess charset, see common.c:print_entry */
		g_printf (_("[%s] %s = %s\n"), source, key, value);
		break;
	}
	case XMMSV_TYPE_LIST:
		g_printf (_("[%s] %s = <list>\n"), source, key);
		break;
	case XMMSV_TYPE_DICT:
		g_printf (_("[%s] %s = <dict>\n"), source, key);
		break;
	case XMMSV_TYPE_COLL:
		g_printf (_("[%s] %s = <coll>\n"), source, key);
		break;
	case XMMSV_TYPE_BIN:
		g_printf (_("[%s] %s = <bin>\n"), source, key);
		break;
	case XMMSV_TYPE_END:
		g_printf (_("[%s] %s = <end>\n"), source, key);
		break;
	case XMMSV_TYPE_NONE:
		g_printf (_("[%s] %s = <none>\n"), source, key);
		break;
	case XMMSV_TYPE_ERROR:
		g_printf (_("[%s] %s = <error>\n"), source, key);
		break;
	}
}

static void
propdict_dump (const gchar *key, xmmsv_t *src_dict, void *udata)
{
	const gchar *keyfilter[] = {key, udata};

	xmmsv_dict_foreach (src_dict, dict_dump, (void *) keyfilter);
}

/* Compare two uint like strcmp */
static gint
compare_uint (gconstpointer a, gconstpointer b, gpointer userdata)
{
	guint va = *((guint *)a);
	guint vb = *((guint *)b);

	if (va < vb) {
		return -1;
	} else if (va > vb) {
		return 1;
	} else {
		return 0;
	}
}


/* Dummy callback that resets the action status as finished. */
void
done (xmmsc_result_t *res, cli_infos_t *infos)
{
	const gchar *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_error (val, &err)) {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

void
tickle (xmmsc_result_t *res, cli_infos_t *infos)
{
	xmmsc_result_t *res2;

	const gchar *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		res2 = xmmsc_playback_tickle (infos->sync);
		xmmsc_result_wait (res2);
		done (res2, infos);
	} else {
		g_printf (_("Server error: %s\n"), err);
		cli_infos_loop_resume (infos);
	}

	xmmsc_result_unref (res);
}

void
list_plugins (cli_infos_t *infos, xmmsc_result_t *res)
{
	const gchar *name, *desc, *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);

		for (xmmsv_list_iter_first (it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {
			xmmsv_t *elem;

			xmmsv_list_iter_entry (it, &elem);
			xmmsv_dict_entry_get_string (elem, "shortname", &name);
			xmmsv_dict_entry_get_string (elem, "description", &desc);

			g_printf ("%s - %s\n", name, desc);
		}
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	xmmsc_result_unref (res);

	cli_infos_loop_resume (infos);
}

static void
print_server_stats (xmmsc_result_t *res)
{
	gint uptime;
	const gchar *version, *err;

	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_dict_entry_get_string (val, "version", &version);
		xmmsv_dict_entry_get_int (val, "uptime", &uptime);
		g_printf ("uptime = %d\n"
		          "version = %s\n", uptime, version);
	} else {
		g_printf ("Server error: %s\n", err);
	}
}

void
print_stats (cli_infos_t *infos, xmmsc_result_t *res)
{
	print_server_stats (res);

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

static void
print_config_entry (const gchar *confname, xmmsv_t *val, void *udata)
{
	xmmsv_type_t type;

	type = xmmsv_get_type (val);

	switch (type) {
	case XMMSV_TYPE_STRING:
	{
		const gchar *confval;
		xmmsv_get_string (val, &confval);
		g_printf ("%s = %s\n", confname, confval);
		break;
	}
	case XMMSV_TYPE_INT32:
	{
		int confval;
		xmmsv_get_int (val, &confval);
		g_printf ("%s = %d\n", confname, confval);
		break;
	}
	default:
		break;
	}
}

void
print_config (cli_infos_t *infos, gchar *confname)
{
	xmmsc_result_t *res;
	const gchar *confval;
	xmmsv_t *val;

	if (confname == NULL) {
		res = xmmsc_config_list_values (infos->sync);
		xmmsc_result_wait (res);
		val = xmmsc_result_get_value (res);
		xmmsv_dict_foreach (val, print_config_entry, NULL);
	} else {
		res = xmmsc_config_get_value (infos->sync, confname);
		xmmsc_result_wait (res);
		val = xmmsc_result_get_value (res);
		xmmsv_get_string (val, &confval);
		print_config_entry (confname, val, NULL);
	}

	cli_infos_loop_resume (infos);

	xmmsc_result_unref (res);
}

void
print_property (cli_infos_t *infos, xmmsc_result_t *res, guint id,
                gchar *source, gchar *property)
{
	if (property == NULL) {
		id_print_info (res, id, source);
	} else {
		/* FIXME(g): print if given an specific property */
	}

	cli_infos_loop_resume (infos);
}

/* Apply operation to an idlist */
static void
apply_ids (cli_infos_t *infos, xmmsc_result_t *res, idlist_command_t cmd)
{
	const gchar *err;
	xmmsc_result_t *cmdres;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);

		for (xmmsv_list_iter_first (it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {
			xmmsv_t *entry;
			gint32 id;

			xmmsv_list_iter_entry (it, &entry);

			if (xmmsv_get_int (entry, &id)) {
				switch (cmd) {
				case IDLIST_CMD_REHASH:
					cmdres = xmmsc_medialib_rehash (infos->sync, id);
					break;
				case IDLIST_CMD_REMOVE:
					cmdres = xmmsc_medialib_remove_entry (infos->sync, id);
					break;
				default:
					break;
				}
				xmmsc_result_wait (cmdres);
				xmmsc_result_unref (cmdres);
			}
		}
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);

	xmmsc_result_unref (res);
}

void
remove_ids (cli_infos_t *infos, xmmsc_result_t *res)
{
	apply_ids (infos, res, IDLIST_CMD_REMOVE);
}


static void
pos_remove_cb (gint pos, void *userdata)
{
	xmmsc_result_t *res;
	pl_pos_udata_t *pack = (pl_pos_udata_t *) userdata;

	res = xmmsc_playlist_remove_entry (pack->infos->sync, pack->playlist, pos);
	xmmsc_result_wait (res);
	xmmsc_result_unref (res);
}

void
positions_remove (cli_infos_t *infos, const gchar *playlist,
                  playlist_positions_t *positions)
{
	pl_pos_udata_t udata = { infos, NULL, playlist, NULL, 0, 0 };
	playlist_positions_foreach (positions, pos_remove_cb, FALSE, &udata);

	cli_infos_loop_resume (infos);
}

void
rehash_ids (cli_infos_t *infos, xmmsc_result_t *res)
{
	apply_ids (infos, res, IDLIST_CMD_REHASH);
}

static void
print_volume_entry (const gchar *key, xmmsv_t *val, void *udata)
{
	gchar *channel = udata;
	gint32 value;

	if (!udata || !strcmp (key, channel)) {
		xmmsv_get_int (val, &value);
		g_printf (_("%s = %u\n"), key, value);
	}
}

void
print_volume (xmmsc_result_t *res, cli_infos_t *infos, gchar *channel)
{
	xmmsv_t *val;
	const gchar *err;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_dict_foreach (val, print_volume_entry, channel);
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);

	xmmsc_result_unref (res);
}

static void
dict_keys (const gchar *key, xmmsv_t *val, void *udata)
{
	GList **list = udata;

	*list = g_list_prepend (*list, g_strdup (key));
}

void
set_volume (cli_infos_t *infos, gchar *channel, gint volume)
{
	xmmsc_result_t *res;
	xmmsv_t *val;
	GList *it, *channels = NULL;

	if (!channel) {
		/* get all channels */
		res = xmmsc_playback_volume_get (infos->sync);
		xmmsc_result_wait (res);
		val = xmmsc_result_get_value (res);
		xmmsv_dict_foreach (val, dict_keys, &channels);
		xmmsc_result_unref (res);
	} else {
		channels = g_list_prepend (channels, g_strdup (channel));
	}

	/* set volumes for channels in list */
	for (it = g_list_first (channels); it != NULL; it = g_list_next (it)) {
		res = xmmsc_playback_volume_set (infos->sync, it->data, volume);
		xmmsc_result_wait (res);
		xmmsc_result_unref (res);

		/* free channel string */
		g_free (it->data);
	}

	g_list_free (channels);

	cli_infos_loop_resume (infos);
}

void
status_mode (cli_infos_t *infos, gchar *format, gint refresh)
{
	status_entry_t *status;

	status = status_init (format, refresh);

	if (refresh > 0) {
		g_printf (_("\n"
		            "   (n) next song\n"
		            "   (p) previous song\n"
		            "   (t) toggle playback\n"
		            "   (ENTER) exit status mode\n\n"));
		cli_infos_status_mode (infos, status);
	} else {
		status_update_all (infos, status);
		status_print_entry (status);
		status_free (status);
		cli_infos_loop_resume (infos);
	}
}

static void
id_print_info (xmmsc_result_t *res, guint id, gchar *source)
{
	xmmsv_t *val;
	const gchar *err;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_dict_foreach (val, propdict_dump, source);
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	xmmsc_result_unref (res);
}

void
list_print_info (xmmsc_result_t *res, cli_infos_t *infos)
{
	xmmsc_result_t *infores = NULL;
	xmmsv_t *val;
	const gchar *err;
	gint32 id;
	gboolean first = true;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);
		while (xmmsv_list_iter_valid (it)) {
			xmmsv_t *entry;

			xmmsv_list_iter_entry (it, &entry);
			if (xmmsv_get_int (entry, &id)) {
				infores = xmmsc_medialib_get_info (infos->sync, id);
				xmmsc_result_wait (infores);

				if (!first) {
					g_printf ("\n");
				} else {
					first = false;
				}
				id_print_info (infores, id, NULL);
			}
			xmmsv_list_iter_next (it);
		}

	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

static void
pos_print_info_cb (gint pos, void *userdata)
{
	xmmsc_result_t *infores;
	pl_pos_udata_t *pack = (pl_pos_udata_t *) userdata;
	guint id;

	/* Skip if outside of playlist */
	if (pos >= pack->infos->cache->active_playlist->len) {
		return;
	}

	id = g_array_index (pack->infos->cache->active_playlist, guint, pos);

	infores = xmmsc_medialib_get_info (pack->infos->sync, id);
	xmmsc_result_wait (infores);

	/* Do not prepend newline before the first entry */
	if (pack->inc > 0) {
		g_printf ("\n");
	} else {
		pack->inc++;
	}
	id_print_info (infores, id, NULL);
}

void
positions_print_info (cli_infos_t *infos, playlist_positions_t *positions)
{
	pl_pos_udata_t udata = { infos, NULL, NULL, NULL, 0, 0 };
	playlist_positions_foreach (positions, pos_print_info_cb, TRUE, &udata);

	cli_infos_loop_resume (infos);
}

void
enrich_mediainfo (xmmsv_t *val)
{
	if (!xmmsv_dict_has_key (val, "title") && xmmsv_dict_has_key (val, "url")) {
		/* First decode the URL encoding */
		xmmsv_t *tmp, *v, *urlv;
		gchar *url = NULL;
		gchar *filename = NULL;
		const unsigned char *burl;
		unsigned int blen;

		xmmsv_dict_get (val, "url", &v);

		tmp = xmmsv_decode_url (v);
		if (tmp && xmmsv_get_bin (tmp, &burl, &blen)) {
			url = g_malloc (blen + 1);
			memcpy (url, burl, blen);
			url[blen] = 0;
			xmmsv_unref (tmp);
			filename = strrchr (url, '/');
			if (!filename || !filename[1]) {
				filename = url;
			} else {
				filename = filename + 1;
			}
		}

		/* Let's see if the result is valid utf-8. This must be done
		 * since we don't know the charset of the binary string */
		if (filename && g_utf8_validate (filename, -1, NULL)) {
			/* If it's valid utf-8 we don't have any problem just
			 * printing it to the screen
			 */
			urlv = xmmsv_new_string (filename);
		} else if (filename) {
			/* Not valid utf-8 :-( We make a valid guess here that
			 * the string when it was encoded with URL it was in the
			 * same charset as we have on the terminal now.
			 *
			 * THIS MIGHT BE WRONG since different clients can have
			 * different charsets and DIFFERENT computers most likely
			 * have it.
			 */
			gchar *tmp2 = g_locale_to_utf8 (filename, -1, NULL, NULL, NULL);
			urlv = xmmsv_new_string (tmp2);
			g_free (tmp2);
		} else {
			/* Decoding the URL failed for some reason. That's not good. */
			urlv = xmmsv_new_string (_("?"));
		}

		xmmsv_dict_set (val, "title", urlv);
		xmmsv_unref (urlv);
		g_free (url);
	}
}

static void
id_coldisp_print_info (cli_infos_t *infos, column_display_t *coldisp, guint id)
{
	xmmsc_result_t *infores;
	xmmsv_t *info;

	infores = xmmsc_medialib_get_info (infos->sync, id);
	xmmsc_result_wait (infores);
	info = xmmsv_propdict_to_dict (xmmsc_result_get_value (infores), NULL);
	enrich_mediainfo (info);
	column_display_print (coldisp, info);

	xmmsc_result_unref (infores);
	xmmsv_unref (info);
}

static void
pos_print_row_cb (gint pos, void *userdata)
{
	pl_pos_udata_t *pack = (pl_pos_udata_t *) userdata;
	guint id;

	if (pos >= pack->entries->len) {
		return;
	}

	id = g_array_index (pack->entries, guint, pos);
	column_display_set_position (pack->coldisp, pos);
	id_coldisp_print_info (pack->infos, pack->coldisp, id);
}

void
positions_print_list (xmmsc_result_t *res, playlist_positions_t *positions,
                      column_display_t *coldisp, gboolean is_search)
{
	cli_infos_t *infos = column_display_infos_get (coldisp);
	pl_pos_udata_t udata = { infos, coldisp, NULL, NULL, 0, 0};
	xmmsv_t *val;
	GArray *entries;

	gint32 id;
	const gchar *err;

	/* FIXME: separate function or merge
	   with list_print_row (lot of if(positions))? */
	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;
		column_display_prepare (coldisp);

		if (is_search) {
			column_display_print_header (coldisp);
		}

		entries = g_array_sized_new (FALSE, FALSE, sizeof (guint),
		                             xmmsv_list_get_size (val));

		for (xmmsv_get_list_iter (val, &it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {
			xmmsv_t *entry;
			xmmsv_list_iter_entry (it, &entry);
			if (xmmsv_get_int (entry, &id)) {
				g_array_append_val (entries, id);
			}
		}

		udata.entries = entries;
		playlist_positions_foreach (positions, pos_print_row_cb, TRUE, &udata);

	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	if (is_search) {
		column_display_print_footer (coldisp);
	} else {
		g_printf ("\n");
		column_display_print_footer_totaltime (coldisp);
	}

	column_display_free (coldisp);
	g_array_free (entries, TRUE);

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

/* Returned tree must be freed by the caller */
static GTree *
matching_ids_tree (xmmsc_result_t *matching)
{
	xmmsv_t *val;
	gint32 id;
	GTree *list = NULL;
	const gchar *err;

	val = xmmsc_result_get_value (matching);

	if (xmmsv_get_error (val, &err) || !xmmsv_is_type (val, XMMSV_TYPE_LIST)) {
		g_printf (_("Error retrieving the media matching the pattern!\n"));
	} else {
		xmmsv_list_iter_t *it;
		xmmsv_t *entry;

		list = g_tree_new_full (compare_uint, NULL, g_free, NULL);

		xmmsv_get_list_iter (val, &it);
		for (xmmsv_list_iter_first (it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {

			xmmsv_list_iter_entry (it, &entry);

			if (xmmsv_get_int (entry, &id)) {
				guint *tid;
				tid = g_new (guint, 1);
				*tid = id;
				g_tree_insert (list, tid, tid);
			}
		}
	}

	return list;
}

void
list_print_row (xmmsc_result_t *res, xmmsv_coll_t *filter,
                column_display_t *coldisp, gboolean is_search)
{
	/* FIXME: w00t at code copy-paste, please modularize */
	cli_infos_t *infos = column_display_infos_get (coldisp);
	xmmsv_t *val;
	GTree *list = NULL;

	const gchar *err;
	gint32 id;
	gint i = 0;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;
		column_display_prepare (coldisp);

		if (filter != NULL) {
			xmmsc_result_t *filres;
			filres = xmmsc_coll_query_ids (infos->sync, filter, NULL, 0, 0);
			xmmsc_result_wait (filres);
			if ((list = matching_ids_tree (filres)) == NULL) {
				goto finish;
			}
		}

		if (is_search) {
			column_display_print_header (coldisp);
		}

		xmmsv_get_list_iter (val, &it);
		while (xmmsv_list_iter_valid (it)) {
			xmmsv_t *entry;
			xmmsv_list_iter_entry (it, &entry);
			if (xmmsv_get_int (entry, &id) &&
			    (!list || g_tree_lookup (list, &id) != NULL)) {
				column_display_set_position (coldisp, i);
				id_coldisp_print_info (infos, coldisp, id);
			}
			xmmsv_list_iter_next (it);
			i++;
		}

	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	if (is_search) {
		column_display_print_footer (coldisp);
	} else {
		g_printf ("\n");
		column_display_print_footer_totaltime (coldisp);
	}

finish:

	if (list) {
		g_tree_destroy (list);
	}
	column_display_free (coldisp);

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

void
coll_rename (cli_infos_t *infos, gchar *oldname, gchar *newname,
             xmmsc_coll_namespace_t ns, gboolean force)
{
	xmmsc_result_t *res;

	if (force) {
		res = xmmsc_coll_remove (infos->sync, newname, ns);
		xmmsc_result_wait (res);
		/* FIXME(g): check something? */
		xmmsc_result_unref (res);
	}

	res = xmmsc_coll_rename (infos->sync, oldname, newname, ns);
	xmmsc_result_wait (res);
	done (res, infos);
}

void
coll_save (cli_infos_t *infos, xmmsv_coll_t *coll,
           xmmsc_coll_namespace_t ns, gchar *name, gboolean force)
{
	xmmsc_result_t *res;
	xmmsv_t *val;
	gboolean save = TRUE;

	if (!force) {
		xmmsv_coll_t *exists;
		res = xmmsc_coll_get (infos->sync, name, ns);
		xmmsc_result_wait (res);
		val = xmmsc_result_get_value (res);
		if (xmmsv_get_coll (val, &exists)) {
			g_printf (_("Error: A collection already exists "
			            "with the target name!\n"));
			save = FALSE;
		}
		xmmsc_result_unref (res);
	}

	if (save) {
		res = xmmsc_coll_save (infos->sync, coll, name, ns);
		xmmsc_result_wait (res);
		done (res, infos);
	} else {
		cli_infos_loop_resume (infos);
	}
}

/* (from src/clients/cli/common.c) */
static void
print_info (const gchar *fmt, ...)
{
	gchar buf[8096];
	va_list ap;

	va_start (ap, fmt);
	g_vsnprintf (buf, 8096, fmt, ap);
	va_end (ap);

	g_printf ("%s\n", buf);
}

/* Produce a GString from the idlist of the collection.
   (must be freed manually!)
   (from src/clients/cli/cmd_coll.c) */
static GString *
coll_idlist_to_string (xmmsv_coll_t *coll)
{
	gint i;
	guint *idlist;
	GString *s;

	s = g_string_new ("(");

	idlist = xmmsc_coll_get_idlist (coll);
	for (i = 0; idlist[i] != 0; ++i) {
		if (i > 0) {
			g_string_append (s, ", ");
		}
		g_string_append_printf (s, "%d", idlist[i]);
	}
	g_string_append_c (s, ')');

	return s;
}

/* (from src/clients/cli/cmd_coll.c) */
static void
coll_dump_list (xmmsv_t *list, unsigned int level)
{
	xmmsv_list_iter_t *it;
	xmmsv_coll_t *operand;
	xmmsv_t *v;

	xmmsv_get_list_iter (list, &it);
	while (xmmsv_list_iter_entry (it, &v)) {
		if (xmmsv_get_coll (v, &operand)) {
			coll_dump (operand, level);
		}
		xmmsv_list_iter_next (it);
	}

}

/* Dump the structure of the collection as a string
   (from src/clients/cli/cmd_coll.c) */
static void
coll_dump (xmmsv_coll_t *coll, guint level)
{
	gint i;
	gchar *indent;

	gchar *attr1;
	gchar *attr2;
	GString *idlist_str;

	indent = g_malloc ((level * 2) + 1);
	for (i = 0; i < level * 2; ++i) {
		indent[i] = ' ';
	}
	indent[i] = '\0';

	/* type */
	switch (xmmsc_coll_get_type (coll)) {
	case XMMS_COLLECTION_TYPE_REFERENCE:
		xmmsc_coll_attribute_get (coll, "reference", &attr1);
		print_info ("%sReference: '%s'", indent, attr1);
		break;

	case XMMS_COLLECTION_TYPE_UNION:
		print_info ("%sUnion:", indent);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_INTERSECTION:
		print_info ("%sIntersection:", indent);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_COMPLEMENT:
		print_info ("%sComplement:", indent);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_EQUALS:
		xmmsc_coll_attribute_get (coll, "field",  &attr1);
		xmmsc_coll_attribute_get (coll, "value", &attr2);
		print_info ("%sEquals ('%s', '%s') for:", indent, attr1, attr2);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_HAS:
		xmmsc_coll_attribute_get (coll, "field",  &attr1);
		print_info ("%sHas ('%s') for:", indent, attr1);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_MATCH:
		xmmsc_coll_attribute_get (coll, "field",  &attr1);
		xmmsc_coll_attribute_get (coll, "value", &attr2);
		print_info ("%sMatch ('%s', '%s') for:", indent, attr1, attr2);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_SMALLER:
		xmmsc_coll_attribute_get (coll, "field",  &attr1);
		xmmsc_coll_attribute_get (coll, "value", &attr2);
		print_info ("%sSmaller ('%s', '%s') for:", indent, attr1, attr2);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_GREATER:
		xmmsc_coll_attribute_get (coll, "field",  &attr1);
		xmmsc_coll_attribute_get (coll, "value", &attr2);
		print_info ("%sGreater ('%s', '%s') for:", indent, attr1, attr2);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	case XMMS_COLLECTION_TYPE_IDLIST:
		idlist_str = coll_idlist_to_string (coll);
		print_info ("%sIdlist: %s", indent, idlist_str->str);
		g_string_free (idlist_str, TRUE);
		break;

	case XMMS_COLLECTION_TYPE_QUEUE:
		idlist_str = coll_idlist_to_string (coll);
		print_info ("%sQueue: %s", indent, idlist_str->str);
		g_string_free (idlist_str, TRUE);
		break;

	case XMMS_COLLECTION_TYPE_PARTYSHUFFLE:
		idlist_str = coll_idlist_to_string (coll);
		print_info ("%sParty Shuffle: %s from :", indent, idlist_str->str);
		g_string_free (idlist_str, TRUE);
		coll_dump_list (xmmsv_coll_operands_get (coll), level + 1);
		break;

	default:
		print_info ("%sUnknown Operator!", indent);
		break;
	}
}
/*  */

void
coll_show (cli_infos_t *infos, xmmsc_result_t *res)
{
	const gchar *err;
	xmmsv_coll_t *coll;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_get_coll (val, &coll);
		coll_dump (coll, 0);
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

static void
print_collections_list (xmmsc_result_t *res, cli_infos_t *infos,
                        gchar *mark, gboolean all)
{
	const gchar *s, *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err)) {
		xmmsv_list_iter_t *it;
		xmmsv_get_list_iter (val, &it);
		while (xmmsv_list_iter_valid (it)) {
			xmmsv_t *entry;
			xmmsv_list_iter_entry (it, &entry);
			/* Skip hidden playlists if all is FALSE*/
			if (xmmsv_get_string (entry, &s) && ((*s != '_') || all)) {
				/* Highlight active playlist */
				if (mark && strcmp (s, mark) == 0) {
					g_printf ("* %s\n", s);
				} else {
					g_printf ("  %s\n", s);
				}
			}
			xmmsv_list_iter_next (it);
		}
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (res);
}

void
list_print_collections (xmmsc_result_t *res, cli_infos_t *infos)
{
	print_collections_list (res, infos, NULL, TRUE);
}

void
list_print_playlists (xmmsc_result_t *res, cli_infos_t *infos, gboolean all)
{
	print_collections_list (res, infos,
	                        infos->cache->active_playlist_name, all);
}

/* Abstract jump, use inc to choose the direction. */
static void
list_jump_rel (xmmsc_result_t *res, cli_infos_t *infos, gint inc)
{
	guint i;
	gint32 id;
	xmmsc_result_t *jumpres = NULL;
	xmmsv_t *val;
	const gchar *err;

	gint currpos;
	gint plsize;
	GArray *playlist;

	currpos = infos->cache->currpos;
	plsize = infos->cache->active_playlist->len;
	playlist = infos->cache->active_playlist;

	/* If no currpos, start jump from beginning */
	if (currpos < 0) {
		currpos = 0;
	}

	val = xmmsc_result_get_value (res);

	if (!xmmsv_get_error (val, &err) && xmmsv_is_type (val, XMMSV_TYPE_LIST)) {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);

		inc += plsize; /* magic trick so we can loop in either direction */

		/* Loop on the playlist */
		for (i = (currpos + inc) % plsize; i != currpos; i = (i + inc) % plsize) {

			/* Loop on the matched media */
			for (xmmsv_list_iter_first (it);
			     xmmsv_list_iter_valid (it);
			     xmmsv_list_iter_next (it)) {
				xmmsv_t *entry;

				xmmsv_list_iter_entry (it, &entry);

				/* If both match, jump! */
				if (xmmsv_get_int (entry, &id)
				    && g_array_index (playlist, guint, i) == id) {
					jumpres = xmmsc_playlist_set_next (infos->sync, i);
					xmmsc_result_wait (jumpres);
					tickle (jumpres, infos);
					goto finish;
				}
			}
		}
	}

	finish:

	/* No matching media found, don't jump */
	if (!jumpres) {
		g_printf (_("No media matching the pattern in the playlist!\n"));
		cli_infos_loop_resume (infos);
	}

	xmmsc_result_unref (res);
}

void
list_jump_back (xmmsc_result_t *res, cli_infos_t *infos)
{
	list_jump_rel (res, infos, -1);
}

void
list_jump (xmmsc_result_t *res, cli_infos_t *infos)
{
	list_jump_rel (res, infos, 1);
}

void
position_jump (cli_infos_t *infos, playlist_positions_t *positions, gboolean dotickle)
{
	xmmsc_result_t *jumpres;
	int pos;

	if (playlist_positions_get_single (positions, &pos)) {
		jumpres = xmmsc_playlist_set_next (infos->sync, pos);
		xmmsc_result_wait (jumpres);
		if (dotickle == TRUE) {
			tickle (jumpres, infos);
		}
	} else {
		g_printf (_("Cannot jump to several positions!\n"));
	}

	cli_infos_loop_resume (infos);
}

void
playback_play (cli_infos_t *infos)
{
	xmmsc_result_t *res;

	res = xmmsc_playback_start (infos->sync);
	xmmsc_result_wait (res);
	done (res, infos);
}

void
playback_pause (cli_infos_t *infos)
{
	xmmsc_result_t *res;

	res = xmmsc_playback_pause (infos->sync);
	xmmsc_result_wait (res);
	done (res, infos);
}

void
playback_toggle (cli_infos_t *infos)
{
	guint status;

	status = infos->cache->playback_status;

	if (status == XMMS_PLAYBACK_STATUS_PLAY) {
		playback_pause (infos);
	} else {
		playback_play (infos);
	}
}

void
set_next_rel (cli_infos_t *infos, gint offset)
{
	xmmsc_result_t *res;

	res = xmmsc_playlist_set_next_rel (infos->sync, offset);
	xmmsc_result_wait (res);
	tickle (res, infos);
}

void
add_pls (xmmsc_result_t *plsres, cli_infos_t *infos,
         gchar *playlist, gint pos)
{
	xmmsc_result_t *res;
	xmmsv_coll_t *coll;
	xmmsv_t *val;
	const char *err;

	val = xmmsc_result_get_value (plsres);

	if (!xmmsv_get_error (val, &err) &&
	    xmmsv_get_coll (val, &coll)) {
		res = xmmsc_playlist_add_idlist (infos->sync, playlist, coll);
		xmmsc_result_wait (res);
		xmmsc_result_unref (res);
	} else {
		g_printf (_("Server error: %s\n"), err);
	}

	xmmsc_result_unref (plsres);
}

void
add_list (xmmsc_result_t *matching, cli_infos_t *infos,
          gchar *playlist, gint pos)

{
	/* FIXME: w00t at code copy-paste, please modularize */
	xmmsc_result_t *insres;
	gint32 id;
	gint offset;
	const gchar *err;

	xmmsv_t *val;

	val = xmmsc_result_get_value (matching);

	offset = 0;

	if (xmmsv_get_error (val, &err) || !xmmsv_is_type (val, XMMSV_TYPE_LIST)) {
		g_printf (_("Error retrieving the media matching the pattern!\n"));
	} else {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);

		/* Loop on the matched media */
		for (xmmsv_list_iter_first (it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {
			xmmsv_t *entry;

			xmmsv_list_iter_entry (it, &entry);

			if (xmmsv_get_int (entry, &id)) {
				insres = xmmsc_playlist_insert_id (infos->sync, playlist,
				                                   pos + offset, id);
				xmmsc_result_wait (insres);
				xmmsc_result_unref (insres);
				offset++;
			}
		}
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (matching);
}

void
move_entries (xmmsc_result_t *matching, cli_infos_t *infos,
              gchar *playlist, gint pos)
{
	xmmsc_result_t *movres, *lisres;
	guint curr;
	gint32 id;
	gint inc;
	gboolean up;
	GTree *list;

	xmmsv_t *lisval;
	const gchar *err;

	lisres = xmmsc_playlist_list_entries (infos->sync, playlist);
	xmmsc_result_wait (lisres);
	lisval = xmmsc_result_get_value (lisres);

	if (xmmsv_get_error (lisval, &err) || !xmmsv_is_type (lisval, XMMSV_TYPE_LIST)) {
			g_printf (_("Error retrieving playlist entries\n"));
	} else {
		xmmsv_list_iter_t *it;
		xmmsv_t *entry;

		/* store matching mediaids in a tree (faster lookup) */
		list = matching_ids_tree (matching);

		/* move matched playlist items */
		curr = 0;
		inc = 0;
		up = TRUE;
		xmmsv_get_list_iter (lisval, &it);
		for (xmmsv_list_iter_first (it);
		     xmmsv_list_iter_valid (it);
		     xmmsv_list_iter_next (it)) {

			xmmsv_list_iter_entry (it, &entry);

			if (curr == pos) {
				up = FALSE;
			}
			if (xmmsv_get_int (entry, &id) &&
			    g_tree_lookup (list, &id) != NULL) {
				if (up) {
					/* moving forward */
					movres = xmmsc_playlist_move_entry (infos->sync, playlist,
					                                    curr - inc, pos - 1);
				} else {
					/* moving backward */
					movres = xmmsc_playlist_move_entry (infos->sync, playlist,
					                                    curr, pos + inc);
				}
				xmmsc_result_wait (movres);
				xmmsc_result_unref (movres);
				inc++;
			}
			curr++;
		}
		g_tree_destroy (list);
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (matching);
	xmmsc_result_unref (lisres);
}

static void
pos_move_cb (gint curr, void *userdata)
{
	xmmsc_result_t *movres;
	pl_pos_udata_t *pack = (pl_pos_udata_t *) userdata;

	/* Entries are moved in descending order, pack->inc is used as
	 * offset both for forward and backward moves, and reset
	 * inbetween. */

	if (curr < pack->pos) {
		/* moving forward */
		if (pack->inc >= 0) {
			pack->inc = -1; /* start inc at -1, decrement */
		}
		movres = xmmsc_playlist_move_entry (pack->infos->sync, pack->playlist,
		                                    curr, pack->pos + pack->inc);
		pack->inc--;
	} else {
		/* moving backward */
		movres = xmmsc_playlist_move_entry (pack->infos->sync, pack->playlist,
		                                    curr + pack->inc, pack->pos);
		pack->inc++;
	}

	xmmsc_result_wait (movres);
	xmmsc_result_unref (movres);
}

void
positions_move (cli_infos_t *infos, const gchar *playlist,
                playlist_positions_t *positions, gint pos)
{
	pl_pos_udata_t udata = { infos, NULL, playlist, NULL, 0, pos };
	playlist_positions_foreach (positions, pos_move_cb, FALSE, &udata);

	cli_infos_loop_resume (infos);
}

void
remove_cached_list (xmmsc_result_t *matching, cli_infos_t *infos)
{
	/* FIXME: w00t at code copy-paste, please modularize */
	xmmsc_result_t *rmres;
	guint plid;
	gint32 id;
	gint plsize;
	GArray *playlist;
	gint i;

	const gchar *err;
	xmmsv_t *val;

	val = xmmsc_result_get_value (matching);

	plsize = infos->cache->active_playlist->len;
	playlist = infos->cache->active_playlist;

	if (xmmsv_get_error (val, &err) || !xmmsv_is_type (val, XMMSV_TYPE_LIST)) {
		g_printf (_("Error retrieving the media matching the pattern!\n"));
	} else {
		xmmsv_list_iter_t *it;

		xmmsv_get_list_iter (val, &it);

		/* Loop on the playlist (backward, easier to remove) */
		for (i = plsize - 1; i >= 0; i--) {
			plid = g_array_index (playlist, guint, i);

			/* Loop on the matched media */
			for (xmmsv_list_iter_first (it);
			     xmmsv_list_iter_valid (it);
			     xmmsv_list_iter_next (it)) {
				xmmsv_t *entry;

				xmmsv_list_iter_entry (it, &entry);

				/* If both match, remove! */
				if (xmmsv_get_int (entry, &id) && plid == id) {
					rmres = xmmsc_playlist_remove_entry (infos->sync, NULL, i);
					xmmsc_result_wait (rmres);
					xmmsc_result_unref (rmres);
					break;
				}
			}
		}
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (matching);
}

void
remove_list (xmmsc_result_t *matchres, xmmsc_result_t *plistres,
                cli_infos_t *infos, gchar *playlist)
{
	/* FIXME: w00t at code copy-paste, please modularize */
	xmmsc_result_t *rmres;
	gint32 plid, id;
	guint i;
	gint offset;

	const gchar *err;
	xmmsv_t *matchval, *plistval;

	matchval = xmmsc_result_get_value (matchres);
	plistval = xmmsc_result_get_value (plistres);

	if (xmmsv_get_error (matchval, &err) || !xmmsv_is_type (matchval, XMMSV_TYPE_LIST)) {
		g_printf (_("Error retrieving the media matching the pattern!\n"));
	} else if (xmmsv_get_error (plistval, &err) || !xmmsv_is_type (plistval, XMMSV_TYPE_LIST)) {
		g_printf (_("Error retrieving the playlist!\n"));
	} else {
		xmmsv_list_iter_t *matchit, *plistit;

		/* FIXME: Can we use a GList to remove more safely in the rev order? */
		offset = 0;
		i = 0;

		xmmsv_get_list_iter (matchval, &matchit);
		xmmsv_get_list_iter (plistval, &plistit);

		/* Loop on the playlist */
		for (xmmsv_list_iter_first (plistit);
		     xmmsv_list_iter_valid (plistit);
		     xmmsv_list_iter_next (plistit)) {
			xmmsv_t *plist_entry;

			xmmsv_list_iter_entry (plistit, &plist_entry);

			if (!xmmsv_get_int (plist_entry, &plid)) {
				plid = 0;  /* failed to get id, should not happen */
			}

			/* Loop on the matched media */
			for (xmmsv_list_iter_first (matchit);
			     xmmsv_list_iter_valid (matchit);
			     xmmsv_list_iter_next (matchit)) {
				xmmsv_t *match_entry;

				xmmsv_list_iter_entry (matchit, &match_entry);

				/* If both match, jump! */
				if (xmmsv_get_int (match_entry, &id) && plid == id) {
					rmres = xmmsc_playlist_remove_entry (infos->sync, playlist,
					                                     i - offset);
					xmmsc_result_wait (rmres);
					xmmsc_result_unref (rmres);
					offset++;
					break;
				}
			}

			i++;
		}
	}

	cli_infos_loop_resume (infos);
	xmmsc_result_unref (matchres);
	xmmsc_result_unref (plistres);
}

void
copy_playlist (xmmsc_result_t *res, cli_infos_t *infos, gchar *playlist)
{
	xmmsc_result_t *saveres;
	xmmsv_coll_t *coll;

	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_coll (val, &coll)) {
		saveres = xmmsc_coll_save (infos->sync, coll, playlist,
		                           XMMS_COLLECTION_NS_PLAYLISTS);
		xmmsc_result_wait (saveres);
		done (saveres, infos);
	} else {
		g_printf (_("Cannot find the playlist to copy!\n"));
		cli_infos_loop_resume (infos);
	}

	xmmsc_result_unref (res);
}

void configure_collection (xmmsc_result_t *res, cli_infos_t *infos,
                           gchar *ns, gchar *name,
                           gchar *attrname, gchar *attrvalue)
{
	xmmsv_coll_t *coll;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_coll (val, &coll)) {
		xmmsc_coll_attribute_set (coll, attrname, attrvalue);
		coll_save (infos, coll, ns, name, TRUE);
	} else {
		g_printf (_("Invalid collection!\n"));
		cli_infos_loop_resume (infos);
	}

	xmmsc_result_unref (res);
}

void
configure_playlist (xmmsc_result_t *res, cli_infos_t *infos, gchar *playlist,
                    gint history, gint upcoming, xmmsv_coll_type_t type,
                    gchar *input)
{
	xmmsc_result_t *saveres;
	xmmsv_coll_t *coll;
	xmmsv_coll_t *newcoll;
	xmmsv_t *val;

	gboolean copied = FALSE;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_coll (val, &coll)) {
		if (xmmsv_coll_get_type (coll) != type) {
			newcoll = coll_copy_retype (coll, type);
			coll = newcoll;
			copied = TRUE;
		}
		if (history >= 0) {
			coll_int_attribute_set (coll, "history", history);
		}
		if (upcoming >= 0) {
			coll_int_attribute_set (coll, "upcoming", upcoming);
		}
		if (input) {
			/* Replace previous operand. */
			newcoll = coll_make_reference (input, XMMS_COLLECTION_NS_COLLECTIONS);
			xmmsv_list_clear (xmmsv_coll_operands_get (coll));
			xmmsv_coll_add_operand (coll, newcoll);
			xmmsv_coll_unref (newcoll);
		}

		saveres = xmmsc_coll_save (infos->sync, coll, playlist,
		                           XMMS_COLLECTION_NS_PLAYLISTS);
		xmmsc_result_wait (saveres);
		done (saveres, infos);
	} else {
		g_printf (_("Cannot find the playlist to configure!\n"));
		cli_infos_loop_resume (infos);
	}

	if (copied) {
		xmmsv_coll_unref (coll);
	}

	xmmsc_result_unref (res);
}

void
collection_print_config (xmmsc_result_t *res, cli_infos_t *infos,
                         gchar *attrname)
{
	xmmsv_coll_t *coll;
	gchar *attrvalue;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_coll (val, &coll)) {
		if (attrname == NULL) {
			xmmsv_dict_foreach (xmmsv_coll_attributes_get (coll),
			                    coll_print_attributes, NULL);
		} else {
			if (xmmsc_coll_attribute_get (coll, attrname, &attrvalue)) {
				g_printf ("[%s] %s\n", attrname, attrvalue);
			} else {
				g_printf (_("Invalid attribute!\n"));
			}
		}
	} else {
		g_printf (_("Invalid collection!\n"));
	}

	cli_infos_loop_resume (infos);

	xmmsc_result_unref (res);
}

void
playlist_print_config (xmmsc_result_t *res, cli_infos_t *infos,
                       gchar *playlist)
{
	xmmsv_coll_t *coll;
	xmmsv_t *val;

	val = xmmsc_result_get_value (res);

	if (xmmsv_get_coll (val, &coll)) {
		pl_print_config (coll, playlist);
	} else {
		g_printf (_("Invalid playlist!\n"));
	}

	cli_infos_loop_resume (infos);

	xmmsc_result_unref (res);
}

gboolean
playlist_exists (cli_infos_t *infos, gchar *playlist)
{
	gboolean retval = FALSE;
	xmmsc_result_t *res;
	xmmsv_t *val;

	res = xmmsc_coll_get (infos->sync, playlist, XMMS_COLLECTION_NS_PLAYLISTS);
	xmmsc_result_wait (res);

	val = xmmsc_result_get_value (res);

	if (!xmmsv_is_error (val)) {
		retval = TRUE;
	}

	xmmsc_result_unref (res);

	return retval;
}

static void
coll_int_attribute_set (xmmsv_coll_t *coll, const char *key, gint value)
{
	gchar buf[MAX_INT_VALUE_BUFFER_SIZE + 1];

	g_snprintf (buf, MAX_INT_VALUE_BUFFER_SIZE, "%d", value);
	xmmsv_coll_attribute_set (coll, key, buf);
}

static xmmsv_coll_t *
coll_make_reference (const char *name, xmmsc_coll_namespace_t ns)
{
	xmmsv_coll_t *ref;

	ref = xmmsv_coll_new (XMMS_COLLECTION_TYPE_REFERENCE);
	xmmsv_coll_attribute_set (ref, "reference", name);
	xmmsv_coll_attribute_set (ref, "namespace", ns);

	return ref;
}

static void
coll_copy_attributes (const char *key, xmmsv_t *val, void *udata)
{
	const char *value;

	if (xmmsv_get_string (val, &value)) {
		xmmsv_coll_attribute_set ((xmmsv_coll_t *) udata, key, value);
	}
}

static void
coll_print_attributes (const char *key, xmmsv_t *val, void *udata)
{
	const char *value;

	if (xmmsv_get_string (val, &value)) {
		g_printf ("[%s] %s\n", key, value);
	}
}

static xmmsv_coll_t *
coll_copy_retype (xmmsv_coll_t *coll, xmmsv_coll_type_t type)
{
	xmmsv_coll_t *copy;
	gint idlistsize;
	gint i;
	guint id;

	copy = xmmsv_coll_new (type);

	idlistsize = xmmsv_coll_idlist_get_size (coll);
	for (i = 0; i < idlistsize; i++) {
		xmmsv_coll_idlist_get_index (coll, i, &id);
		xmmsv_coll_idlist_append (copy, id);
	}

	xmmsv_dict_foreach (xmmsv_coll_attributes_get (coll),
	                    coll_copy_attributes, copy);

	return copy;
}

static void
pl_print_config (xmmsv_coll_t *coll, const char *name)
{
	xmmsv_coll_t *op;
	xmmsv_coll_type_t type;
	gchar *upcoming = NULL;
	gchar *history = NULL;
	gchar *input = NULL;
	gchar *input_ns = NULL;
	xmmsv_t *v;

	type = xmmsv_coll_get_type (coll);

	xmmsv_coll_attribute_get (coll, "upcoming", &upcoming);
	xmmsv_coll_attribute_get (coll, "history", &history);

	g_printf (_("name: %s\n"), name);

	switch (type) {
	case XMMS_COLLECTION_TYPE_IDLIST:
		g_printf (_("type: list\n"));
		break;
	case XMMS_COLLECTION_TYPE_QUEUE:
		g_printf (_("type: queue\n"));
		g_printf (_("history: %s\n"), history);
		break;
	case XMMS_COLLECTION_TYPE_PARTYSHUFFLE:
		if (xmmsv_list_get (xmmsv_coll_operands_get (coll), 0, &v) &&
		    xmmsv_get_coll (v, &op)) {
			xmmsv_coll_attribute_get (op, "reference", &input);
			xmmsv_coll_attribute_get (op, "namespace", &input_ns);
		}

		g_printf (_("type: pshuffle\n"));
		g_printf (_("history: %s\n"), history);
		g_printf (_("upcoming: %s\n"), upcoming);
		g_printf (_("input: %s/%s\n"), input_ns, input);
		break;
	default:
		g_printf (_("type: unknown!\n"));
		break;
	}
}

void
print_padding (gint length, gchar padchar)
{
	while (length-- > 0) {
		g_printf ("%c", padchar);
	}
}

void
print_indented (const gchar *string, guint level)
{
	gboolean indent = TRUE;
	const gchar *c;

	for (c = string; *c; c++) {
		if (indent) {
			print_padding (level, ' ');
			indent = FALSE;
		}
		g_printf ("%c", *c);
		if (*c == '\n') {
			indent = TRUE;
		}
	}
}

/* FIXME: not portable? */
gint
find_terminal_width ()
{
	gint columns;
	struct winsize ws;
	char *colstr, *endptr;

	if (!isatty (STDOUT_FILENO)) {
		columns = 0;
	} else if (!ioctl (STDIN_FILENO, TIOCGWINSZ, &ws)) {
		columns = ws.ws_col;
	} else {
		colstr = getenv ("COLUMNS");
		if (colstr != NULL) {
			columns = strtol (colstr, &endptr, 10);
		}
	}

	/* Default to 80 columns */
	if (columns <= 0) {
		columns = 80;
	}

	return columns;
}

/* Returned string must be freed by the caller */
gchar *
format_time (guint64 duration, gboolean use_hours)
{
	guint64 hour, min, sec;
	gchar *time;

	/* +500 for rounding */
	sec = (duration+500) / 1000;
	min = sec / 60;
	sec = sec % 60;

	if (use_hours) {
		hour = min / 60;
		min = min % 60;
		time = g_strdup_printf ("%llu:%02llu:%02llu", hour, min, sec);
	} else {
		time = g_strdup_printf ("%02llu:%02llu", min, sec);
	}

	return time;
}

gchar *
decode_url (const gchar *string)
{
	gint i = 0, j = 0;
	gchar *url;

	url = g_strdup (string);
	if (!url)
		return NULL;

	while (url[i]) {
		guchar chr = url[i++];

		if (chr == '+') {
			chr = ' ';
		} else if (chr == '%') {
			gchar ts[3];
			gchar *t;

			ts[0] = url[i++];
			if (!ts[0])
				goto err;
			ts[1] = url[i++];
			if (!ts[1])
				goto err;
			ts[2] = '\0';

			chr = strtoul (ts, &t, 16);

			if (t != &ts[2])
				goto err;
		}

		url[j++] = chr;
	}

	url[j] = '\0';

	return url;

 err:
	g_free (url);
	return NULL;
}
