#ifndef __XMMS_CONFIG_H
#define __XMMS_CONFIG_H

#include <glib.h>

/* all hashes here are filled with config_value structs */
typedef struct xmms_config_data_St {
	GHashTable *core;
	GHashTable *decoder;
	GHashTable *output;
	GHashTable *transport;
	GHashTable *effect;
} xmms_config_data_t;

#define XMMS_CONFIG_VALUE_PLAIN 0
#define XMMS_CONFIG_VALUE_LIST 1

#define XMMS_MAX_CONFIGFILE_LEN 255

#define XMMS_CONFIG_SYSTEMWIDE SYSCONFDIR "/xmms2.conf"

typedef struct xmms_config_value_St xmms_config_value_t;

typedef GHashTable xmms_config_info_t;

/* forward declare */
xmms_config_data_t *xmms_config_init (gchar *filename);
gboolean xmms_config_save_to_file (xmms_config_data_t *config, gchar *filename);

void xmms_config_deinit(xmms_config_data_t *data);

xmms_config_value_t *xmms_config_value_get_from_list (xmms_config_value_t *value, gchar *directive);

xmms_config_value_t *xmms_config_data_get (xmms_config_info_t *config, 
			                  gchar *key);

xmms_config_value_t *xmms_config_value_lookup (GHashTable *h, const gchar *valuename);

void xmms_config_set (gchar *key, gchar *value);

/* misc support functions */

xmms_config_value_t *xmms_config_value_create (gint type, gchar *directive);

void xmms_config_value_data_set (xmms_config_value_t *value, 
				 gchar *data);

gchar *xmms_config_value_name_get (xmms_config_value_t *value);

xmms_config_value_t *xmms_config_value_list_lookup (xmms_config_value_t *value,
					 	    const gchar *subvalue);

xmms_config_value_t *xmms_config_value_property_lookup (xmms_config_value_t *val, const gchar *property);

void xmms_config_value_list_add ( xmms_config_value_t *value, 
				  xmms_config_value_t *add);


gchar *xmms_config_value_getprop (xmms_config_value_t *value,
				  gchar *propname);


void xmms_config_value_prop_add (xmms_config_value_t *value,
				  gchar *propname,
				  gchar *data);

gint xmms_config_value_type (xmms_config_value_t *value);
gint xmms_config_value_as_int (xmms_config_value_t *value);
gchar *xmms_config_value_as_string (xmms_config_value_t *value);

gboolean xmms_config_add_property (xmms_config_value_t *val, gchar *name, gchar *data);
xmms_config_value_t *xmms_config_add_section (GHashTable *h, gchar *name);

#endif

