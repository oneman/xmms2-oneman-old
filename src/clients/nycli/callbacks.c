/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2007 XMMS2 Team
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

#include "callbacks.h"

#include "cli_infos.h"


/* Dumps a propdict on stdout */
static void
propdict_dump (const void *key, xmmsc_result_value_type_t type,
               const void *value, const char *source, void *udata)
{
	switch (type) {
	case XMMSC_RESULT_VALUE_TYPE_UINT32:
		printf ("[%s] %s = %u\n", source, key, (uint32_t*) value);
		break;
	case XMMSC_RESULT_VALUE_TYPE_INT32:
		printf ("[%s] %s = %d\n", source, key, (int32_t*) value);
		break;
	case XMMSC_RESULT_VALUE_TYPE_STRING:
		printf ("[%s] %s = %s\n", source, key, (gchar*) value);
		break;
	case XMMSC_RESULT_VALUE_TYPE_DICT:
		printf ("[%s] %s = <dict>\n", source, key);
		break;
	case XMMSC_RESULT_VALUE_TYPE_PROPDICT:
		printf ("[%s] %s = <propdict>\n", source, key);
		break;
	case XMMSC_RESULT_VALUE_TYPE_COLL:
		printf ("[%s] %s = <coll>\n", source, key);
		break;
	case XMMSC_RESULT_VALUE_TYPE_BIN:
		printf ("[%s] %s = <bin>\n", source, key);
		break;
	case XMMSC_RESULT_VALUE_TYPE_NONE:
		printf ("[%s] %s = <unknown>\n", source, key);
		break;
	}
}


/* Dummy callback that resets the action status as finished. */
void
cb_done (xmmsc_result_t *res, void *udata)
{
	cli_infos_t *infos = (cli_infos_t *) udata;
	cli_infos_loop_resume (infos);
}

void
cb_tickle (xmmsc_result_t *res, void *udata)
{
	cli_infos_t *infos = (cli_infos_t *) udata;

	if (!xmmsc_result_iserror (res)) {
		xmmsc_playback_tickle (infos->conn);
	} else {
		printf ("Server error: %s\n", xmmsc_result_get_error (res));
	}

	cli_infos_loop_resume (infos);
}

void
cb_id_print_info_notdone (xmmsc_result_t *res, void *udata)
{
	if (!xmmsc_result_iserror (res)) {
		xmmsc_result_propdict_foreach (res, propdict_dump, NULL);
		printf ("\n");
	} else {
		printf ("Server error: %s\n", xmmsc_result_get_error (res));
	}
}

void
cb_id_print_info (xmmsc_result_t *res, void *udata)
{
	cli_infos_t *infos = (cli_infos_t *) udata;

	cb_id_print_info_notdone (res, udata);
	cli_infos_loop_resume (infos);
}

void
cb_list_print_info (xmmsc_result_t *res, void *udata)
{
	cli_infos_t *infos = (cli_infos_t *) udata;
	xmmsc_result_t *infores = NULL;
	gint id;

	if (!xmmsc_result_iserror (res)) {
		while (xmmsc_result_list_valid (res)) {
			if (xmmsc_result_get_uint (res, &id)) {
				infores = xmmsc_medialib_get_info (infos->conn, id);
				xmmsc_result_notifier_set (infores, cb_id_print_info_notdone, infos);
			}
			xmmsc_result_list_next (res);
		}

		/* Override the last callback to resume action when done */
		if (infores) {
			xmmsc_result_notifier_set (infores, cb_id_print_info, infos);
		}

	} else {
		printf ("Server error: %s\n", xmmsc_result_get_error (res));
	}

	/* No resume-callback pending, we're done */
	if (!infores) {
		cli_infos_loop_resume (infos);
	}
}