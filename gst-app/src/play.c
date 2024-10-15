/* Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* sorry. *nix only */
#include <fcntl.h>
#include <unistd.h>

#include "play.h"
#include "typefind-hack.h"

#define KERNAL_SIZE (8*1024)
#define BASIC_SIZE (8*1024)
#define CHARGEN_SIZE (4*1024)


/*
   Loads rom file. Return NULL if fails.
 */
static GByteArray *_load_rom (const gchar *name, gsize rom_size)
{
    GByteArray *a = NULL;
    ssize_t len;
    int fd = -1;
    guint8 *buf = NULL;
    if (name == NULL) goto load_rom_error;
    fd  = open (name, O_RDONLY, 0);
    if (fd < 0) goto load_rom_error;

    buf = g_malloc0 (rom_size);
    if (buf == NULL) goto load_rom_error;

    a = g_byte_array_new ();
    if (a == NULL) goto load_rom_error;

    while ((len = read (fd, buf, rom_size)) > 0) {
        g_byte_array_append (a, buf, len);
    }

    if (a->len != rom_size) {
        g_byte_array_free (a, TRUE);
        a = NULL;
    }
load_rom_error:
    g_free (buf);
    if (fd > -1) close (fd);
    return a;
}



static void _on_element_added (GstBin *p0, GstBin *p1, GstElement *e, gpointer data)
{
    gchar *name = gst_element_get_name (e);
    if (g_str_has_prefix (name, "siddecfp") == TRUE) {
        g_object_set (G_OBJECT (e),
#if 0
            These can be tested with gst-launch

            "tune", _tune_index,
            "filter", _filter,
            "sid-model", _sid_model,
            "force-sid-model", _force_sid_model,
            "c64-model", _c64_model,
            "force-c64-model", _force_c64_model,
            "cia-model", _cia_model,
            "digi-boost", _digiboost,
            "sampling-method", _sampling_method,
            "filter-bias", _filter_bias,
            "filter-curve-6581", _filter_curve_6581,
            "filter-curve-8580", _filter_curve_8580,
#endif
            /*
               Some RSIDs requires some ROM files. Like Wally Bebens Tetris.sid
               requires kernal.bin kernal-906145-02.bin works fine with it.

               Direct links:
               https://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/kernal.906145-02.bin
               - try first without and then place to workingdir and rename to kernal.bin

               https://hvsc.brona.dk/HVSC/C64Music/MUSICIANS/B/Beben_Wally/Tetris.sid
               - use as any song
             */

            "basic", _load_rom ("basic.bin", BASIC_SIZE),
            "kernal", _load_rom ("kernal.bin", KERNAL_SIZE),
            "chargen", _load_rom ("chargen.bin", CHARGEN_SIZE),
            NULL);
    }
}

void
play_uri (const gchar * uri)
{
  GstStateChangeReturn sret;
  GstElement *playbin;
  GstElement *audiosink;
  GstMessage *msg = NULL;
  GstBus *bus;

  g_print ("Trying to play %s ...\n", uri);

  typefind_hack_init ();

  playbin = gst_element_factory_make ("playbin", "playbin");
  if (playbin == NULL)
    goto no_playbin;

  /* get playbin's bus - we'll watch it for messages */
  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));

  /* set audio sink */
  audiosink = gst_element_factory_make ("autoaudiosink", "audiosink");
  if (audiosink == NULL)
    goto no_autoaudiosink;
  g_object_set (playbin, "audio-sink", audiosink, NULL);

  /* set URI to play back */
  g_object_set (playbin, "uri", uri, NULL);

  /* to set some values for sidfp-plugin */
  g_signal_connect (GST_BIN (playbin), "deep-element-added", G_CALLBACK (_on_element_added), NULL);

  /* and GO GO GO! */
  gst_element_set_state (GST_ELEMENT (playbin), GST_STATE_PLAYING);

  /* wait (blocks!) until state change either completes or fails */
  sret = gst_element_get_state (GST_ELEMENT (playbin), NULL, NULL, -1);

  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:{
      msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, 0);
      goto got_error_message;
    }
    case GST_STATE_CHANGE_SUCCESS:{
      GstMessage *msg;

      g_print ("Playing ...\n");

      while (1) {
        gint64 dur, pos;

        if (gst_element_query_duration (playbin, GST_FORMAT_TIME, &dur) &&
            gst_element_query_position (playbin, GST_FORMAT_TIME, &pos)) {
          g_print ("  %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\n",
              GST_TIME_ARGS (pos), GST_TIME_ARGS (dur));
        }

        /* check if we finished or if there was an error,
         * but don't wait/block if neither is the case */
        msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, 0);

        if (msg && GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
          goto got_error_message;

        if (msg && GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
          g_print ("Finished.\n");
          break;
        }

        /* sleep for one second */
        g_usleep (G_USEC_PER_SEC * 1);
      }
      break;
    }
    default:
      g_assert_not_reached ();
  }

  /* shut down and free everything */
  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
  gst_object_unref (bus);
  return;

/* ERRORS */
got_error_message:
  {
    if (msg) {
      GError *err = NULL;
      gchar *dbg_str = NULL;

      gst_message_parse_error (msg, &err, &dbg_str);
      g_printerr ("FAILED to play %s: %s\n%s\n", uri, err->message,
          (dbg_str) ? dbg_str : "(no debugging information)");
      g_error_free (err);
      g_free (dbg_str);
      gst_message_unref (msg);
    } else {
      g_printerr ("FAILED to play %s: unknown error\n", uri);
    }

    /* shut down and free everything */
    gst_element_set_state (playbin, GST_STATE_NULL);
    gst_object_unref (playbin);
    gst_object_unref (bus);
    return;
  }

no_playbin:
  {
    g_error ("Could not create GStreamer 'playbin' element. "
        "Please install it");
    /* not reached, g_error aborts */
    return;
  }

no_autoaudiosink:
  {
    g_error ("Could not create GStreamer 'autoaudiosink' element. "
        "Please install it");
    /* not reached, g_error aborts */
    return;
  }

}


