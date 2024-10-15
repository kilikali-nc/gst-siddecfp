/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <gst/gst.h>
#include <gst/gsttypefind.h>

#include "typefind-hack.h"

/* copy-paste from gst-typefind plugin. TODO: make patch for gstreamer. */
typedef struct
{
    const guint8 *data;
    guint size;
    guint probability;
    GstCaps *caps;
}
GstTypeFindData;

static void _sw_data_destroy (GstTypeFindData * sw_data);
static void _start_with_type_find (GstTypeFind * tf, gpointer private);

void typefind_hack_init (void)
{
    GstTypeFindData *sw_data = g_slice_new (GstTypeFindData);
    sw_data->data = (const guint8 *)"RSID";
    sw_data->size = 4;
    sw_data->probability = GST_TYPE_FIND_MAXIMUM;
    sw_data->caps = gst_caps_new_empty_simple ("audio/x-rsid");
    if (!gst_type_find_register (NULL, "sidfp", GST_RANK_MARGINAL, _start_with_type_find,
            "sid", sw_data->caps, sw_data, (GDestroyNotify) (_sw_data_destroy))) {
        _sw_data_destroy (sw_data);
    }
}

static void _sw_data_destroy (GstTypeFindData * sw_data)
{
    if (G_LIKELY (sw_data->caps != NULL)) gst_caps_unref (sw_data->caps);
    g_slice_free (GstTypeFindData, sw_data);
}

static void _start_with_type_find (GstTypeFind * tf, gpointer private)
{
    GstTypeFindData *start_with = (GstTypeFindData *) private;
    const guint8 *data;

    GST_LOG ("trying to find mime type %s with the first %u bytes of data",
        gst_structure_get_name (gst_caps_get_structure (start_with->caps, 0)),
        start_with->size);
    data = gst_type_find_peek (tf, 0, start_with->size);
    if (data && memcmp (data, start_with->data, start_with->size) == 0) {
        gst_type_find_suggest (tf, start_with->probability, start_with->caps);
    }
}
