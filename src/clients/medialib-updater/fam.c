#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <fam.h>

#include "mlibupdater.h"

#include <glib.h>

typedef struct fam_data_St {
	FAMConnection fc;
} fam_data_t;

gboolean
monitor_process (xmonitor_t *mon, xevent_t *event)
{
	fam_data_t *fdata = mon->data;
	FAMEvent fe;

	if (FAMPending (&fdata->fc)) {
		if (FAMNextEvent (&fdata->fc, &fe) < 0) {
			ERR ("Error in NextEvent!");
			return FALSE;
		}

		g_snprintf (event->filename, MON_FILENAME_MAX, "%s/%s", (gchar *)fe.userdata, fe.filename);
		switch (fe.code) {
			case FAMChanged:
				event->code = MON_DIR_CHANGED;
				break;
			case FAMDeleted:
				event->code = MON_DIR_DELETED;
				break;
			case FAMCreated:
				event->code = MON_DIR_CREATED;
				break;
			case FAMMoved:
				event->code = MON_DIR_MOVED;
				break;
			default:
				DBG ("unhandled code %d", fe.code);
		}
		return TRUE;
	}

	return FALSE;
}

gboolean
monitor_add_dir (xmonitor_t *mon, gchar *dir, gboolean recursive)
{
	fam_data_t *fdata = mon->data;
	FAMRequest fr;

	if (FAMMonitorDirectory (&fdata->fc, dir, &fr, g_strdup (dir)) < 0) {
		ERR ("Couldn't watch dir %s", dir);
		return FALSE;
	}

	return TRUE;
}

gint
monitor_init (xmonitor_t *mon)
{
	fam_data_t *fdata;

	fdata = g_new0 (fam_data_t, 1);
	if (FAMOpen (&fdata->fc) < 0) {
		ERR ("Couldn't initalize FAM");
		return -1;
	}

	mon->data = (gpointer) fdata;

	return fdata->fc.fd;
}

