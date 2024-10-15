/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *           (C) <2006> Wim Taymans <wim@fluendo.com>
 *           (C) <2022> Joni Valtanen <jvaltane@kapsi.fi>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-siddecfp
 *
 * This element decodes .sid files to raw audio. .sid files are in fact
 * small Commodore 64 programs that are executed on an emulated 6502 CPU and a
 * SID sound chip (MOS6581 or MOS8580).
 *
 * This plugin will first load the complete program into memory before starting
 * the emulator and producing output.
 *
 * To play RSID files: kernal, basic and possibly chargen ROM byte arrays should
 * be set. PSID files works without those.
 *
 * Seeking is not (and cannot be) implemented.
 *
 * ## Example pipelines
 *
 * |[
 * gst-launch-1.0 -v filesrc location=Delta.sid ! siddecfp ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode a sid file and play it back.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sidplayfp/builders/resid.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/SidTuneInfo.h>

#include <string.h>
#include <gst/audio/audio.h>
#include "gstsiddecfp.h"

#define DEFAULT_EMULATION SIDDECFP_EMULATION_RESIDFP
#define DEFAULT_TUNE 0
#define DEFAULT_FILTER TRUE
#define DEFAULT_SID_MODEL SidConfig::MOS6581
#define DEFAULT_C64_MODEL SidConfig::PAL
#define DEFAULT_CIA_MODEL SidConfig::MOS6526
#define DEFAULT_FORCE_SID_MODEL FALSE
#define DEFAULT_FORCE_C64_MODEL FALSE
#define DEFAULT_SAMPLING_METHOD SidConfig::INTERPOLATE
#define DEFAULT_DIGI_BOOST FALSE
#define DEFAULT_FILTER_CURVE_6581 0.5
#define DEFAULT_FILTER_CURVE_8580 0.5
#define DEFAULT_FILTER_BIAS 0.5
#define DEFAULT_BLOCKSIZE 4096

#define MAX_SID_TUNE_BUF_SIZE (8*DEFAULT_BLOCKSIZE) /* more than enough */


enum
{
  PROP_0,
  PROP_EMULATION,
  PROP_TUNE,
  PROP_N_TUNES,
  PROP_FILTER,
  PROP_C64_MODEL,
  PROP_SID_MODEL,
  PROP_CIA_MODEL,
  PROP_FORCE_SID_MODEL,
  PROP_FORCE_C64_MODEL,
  PROP_SAMPLING_METHOD,
  PROP_DIGI_BOOST,
  PROP_FILTER_CURVE_6581,
  PROP_FILTER_CURVE_8580,
  PROP_FILTER_BIAS,
  PROP_KERNAL,
  PROP_BASIC,
  PROP_CHARGEN,
  PROP_BLOCKSIZE,
  PROP_METADATA
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sid; audio/x-rsid")
    );

#define FORMATS "{ " GST_AUDIO_NE(S16) " }"

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
      "format = (string) " FORMATS ", "
      "layout = (string) interleaved, "
      "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_siddecfp_debug);
#define GST_CAT_DEFAULT gst_siddecfp_debug

#define GST_TYPE_EMULATION (gst_emulation_get_type())
static GType
gst_emulation_get_type (void)
{
  static GType emulation_type = 0;
  static const GEnumValue emulation[] = {
    {SIDDECFP_EMULATION_RESIDFP, "RESIDFP", "residfp"},
    {SIDDECFP_EMULATION_RESID, "RESID", "resid"},
    {0, NULL, NULL},
  };

  if (!emulation_type) {
    emulation_type = g_enum_register_static ("GstSidDecFpEmulation", emulation);
  }
  return emulation_type;
}

#define GST_TYPE_SID_MODEL (gst_sid_model_get_type())
static GType
gst_sid_model_get_type (void)
{
  static GType sid_model_type = 0;
  static const GEnumValue sid_model[] = {
    {SidConfig::MOS6581, "MOS6581", "mos6581"},
    {SidConfig::MOS8580, "MOS8580", "mos8580"},
    {0, NULL, NULL},
  };

  if (!sid_model_type) {
    sid_model_type = g_enum_register_static ("GstSidDecFpSidModel", sid_model);
  }
  return sid_model_type;
}

#define GST_TYPE_C64_MODEL (gst_c64_model_get_type())
static GType
gst_c64_model_get_type (void)
{
  static GType c64_model_type = 0;
  static const GEnumValue c64_model[] = {
    {SidConfig::PAL, "PAL", "pal"},
    {SidConfig::NTSC, "NTSC", "ntsc"},
    {SidConfig::OLD_NTSC, "OLD-NTSC", "old-ntsc"},
    {SidConfig::DREAN, "DREAN", "drean"},
    {SidConfig::PAL_M, "PALM", "pal-m"},
    {0, NULL, NULL},
  };

  if (!c64_model_type) {
    c64_model_type = g_enum_register_static ("GstSidDecFpC64Model", c64_model);
  }
  return c64_model_type;
}

#define GST_TYPE_SAMPLING_METHOD (gst_sampling_method_get_type())
static GType
gst_sampling_method_get_type (void)
{
  static GType sampling_method_type = 0;
  static const GEnumValue sampling_method[] = {
    {SidConfig::INTERPOLATE, "INTERPOLATE", "interpolate"},
    {SidConfig::RESAMPLE_INTERPOLATE, "RESAMPLE_INTERPOLATE", "resample-interpolate"},
    {0, NULL, NULL},
  };

  if (!sampling_method_type) {
    sampling_method_type = g_enum_register_static ("GstSidDecFpSamplingMethod", sampling_method);
  }
  return sampling_method_type;
}

#define GST_TYPE_CIA_MODEL (gst_cia_model_get_type())
static GType
gst_cia_model_get_type (void)
{
  static GType cia_model_type = 0;
  static const GEnumValue cia_model[] = {
    {SidConfig::MOS6526, "MOS6526", "mos6526"},
    {SidConfig::MOS8521, "MOS8521", "mos8521"},
    {SidConfig::MOS6526W4485, "MOS6526W4485", "mos6526w4485"},
    {0, NULL, NULL},
  };

  if (!cia_model_type) {
    cia_model_type = g_enum_register_static ("GstSidDecFpCiaModel", cia_model);
  }
  return cia_model_type;
}


static void gst_siddecfp_finalize (GObject * object);

static GstFlowReturn gst_siddecfp_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_siddecfp_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_siddecfp_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_siddecfp_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_siddecfp_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_siddecfp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_siddecfp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_siddecfp_parent_class parent_class
G_DEFINE_TYPE (GstSidDecFp, gst_siddecfp, GST_TYPE_ELEMENT);

static void
gst_siddecfp_class_init (GstSidDecFpClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_siddecfp_finalize;
  gobject_class->set_property = gst_siddecfp_set_property;
  gobject_class->get_property = gst_siddecfp_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_EMULATION,
      g_param_spec_enum ("emulation", "Emulation", "Select libsidplayfp emulation",
          GST_TYPE_EMULATION, DEFAULT_EMULATION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TUNE,
      g_param_spec_int ("tune", "Tune", "Select tune tune",
          0, 100, DEFAULT_TUNE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_N_TUNES,
      g_param_spec_int ("n-tunes", "Number of tunes", "Get number of tunes",
          0, 100, 0,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER,
      g_param_spec_boolean ("filter", "Filter", "Force filter", DEFAULT_FILTER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_C64_MODEL,
      g_param_spec_enum ("c64-model", "C64 model", "Select default C64 model",
          GST_TYPE_C64_MODEL, DEFAULT_C64_MODEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SID_MODEL,
      g_param_spec_enum ("sid-model", "SID model", "Select default SID model",
          GST_TYPE_SID_MODEL, DEFAULT_SID_MODEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CIA_MODEL,
      g_param_spec_enum ("cia-model", "CIA model", "Select default CIA model",
          GST_TYPE_CIA_MODEL, DEFAULT_CIA_MODEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORCE_SID_MODEL,
      g_param_spec_boolean ("force-sid-model", "Force SID model",
          "Forces SID model ot be used even tune uses different",
          DEFAULT_FORCE_SID_MODEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORCE_C64_MODEL,
      g_param_spec_boolean ("force-c64-model", "Force c64 model",
          "Forces C64 model to be used even tune uses different", DEFAULT_FORCE_C64_MODEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SAMPLING_METHOD,
      g_param_spec_enum ("sampling-method", "Sampling method",
          "Select sampling method",
          GST_TYPE_SAMPLING_METHOD, DEFAULT_SAMPLING_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DIGI_BOOST,
      g_param_spec_boolean ("digi-boost", "Digi boost", "Enable digi boost for 8580",
          DEFAULT_DIGI_BOOST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_CURVE_6581,
      g_param_spec_double ("filter-curve-6581", "Filter curve 6581",
          "Filter curve 6581. ReSIDfp emulaton only",
          0, 1.0, DEFAULT_FILTER_CURVE_6581,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_CURVE_8580,
      g_param_spec_double ("filter-curve-8580", "Filter curve 8580",
          "Filter curve 8580. ReSIDfp emulaton only",
          0, 1.0, DEFAULT_FILTER_CURVE_8580,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_BIAS,
      g_param_spec_double ("filter-bias", "Filter bias",
          "Filter bias is given in millivolts. ReSID emulaton only",
          -600.0, 600, DEFAULT_FILTER_BIAS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Block size",
          "Size in bytes to output per buffer", 1, G_MAXUINT,
          DEFAULT_BLOCKSIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_KERNAL,
      g_param_spec_boxed ("kernal", "Kernal ROM", "Kernal ROM byte array. (8192 bytes)",
          G_TYPE_BYTE_ARRAY,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BASIC,
      g_param_spec_boxed ("basic", "Basic ROM", "Basic ROM byte array. (8192 bytes)",
          G_TYPE_BYTE_ARRAY,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CHARGEN,
      g_param_spec_boxed ("chargen", "Chargen ROM", "Chargen ROM byte array. (8192 bytes)",
          G_TYPE_BYTE_ARRAY,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_METADATA,
      g_param_spec_boxed ("metadata", "Metadata", "Metadata", GST_TYPE_CAPS,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class, "C64 SID decoder",
      "Codec/Decoder/Audio", "Use libsidplayfp to decode SID audio tunes",
      "Joni Valtanen <jvaltane@kapsi.fi>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_templ);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_templ);

  GST_DEBUG_CATEGORY_INIT (gst_siddecfp_debug, "siddecfp", 0,
      "C64 SID song player");

  gst_type_mark_as_plugin_api (GST_TYPE_EMULATION, static_cast<GstPluginAPIFlags>(0));
  gst_type_mark_as_plugin_api (GST_TYPE_C64_MODEL, static_cast<GstPluginAPIFlags>(0));
  gst_type_mark_as_plugin_api (GST_TYPE_SID_MODEL, static_cast<GstPluginAPIFlags>(0));
  gst_type_mark_as_plugin_api (GST_TYPE_CIA_MODEL, static_cast<GstPluginAPIFlags>(0));
  gst_type_mark_as_plugin_api (GST_TYPE_SAMPLING_METHOD, static_cast<GstPluginAPIFlags>(0));
}

static gboolean
is_sidbuilder_valid (sidbuilder *builder)
{
    if (builder == NULL) return FALSE;
    if (!builder->getStatus ()) return FALSE;
    return TRUE;
}

static gboolean
create_builder (GstSidDecFp * siddecfp)
{
  sidbuilder *builder = siddecfp->config.sidEmulation;
  siddecfp->config.sidEmulation = NULL;
  if (builder != NULL) {
    delete builder;
    builder = NULL;
  }
  if (siddecfp->emulation == SIDDECFP_EMULATION_RESIDFP) {
    ReSIDfpBuilder *rsfp = new ReSIDfpBuilder ("ReSIDfp");
    if (!is_sidbuilder_valid (rsfp)) {
      return FALSE;
    }
    rsfp->create ((siddecfp->player->info ()).maxsids ());
    if (!is_sidbuilder_valid (rsfp)) {
      return FALSE;
    }
    rsfp->filter (false);
    if (!is_sidbuilder_valid (rsfp)) {
      return FALSE;
    }

    rsfp->filter6581Curve (siddecfp->filter_curve_6581);
    rsfp->filter8580Curve (siddecfp->filter_curve_8580);

    builder = rsfp;

    GST_DEBUG_OBJECT (siddecfp, "using ReSIDfp emulation");
  } else if (siddecfp->emulation == SIDDECFP_EMULATION_RESID) {
    ReSIDBuilder *rs = new ReSIDBuilder("ReSID");
    if (!is_sidbuilder_valid (rs)) {
      return FALSE;
    }
    rs->create ((siddecfp->player->info ()).maxsids ());
    if (!is_sidbuilder_valid (rs)) {
      return FALSE;
    }
    rs->filter (false);
    if (!is_sidbuilder_valid (rs)) {
      return FALSE;
    }
    rs->bias (siddecfp->filter_bias);

    builder = rs;

    GST_DEBUG_OBJECT (siddecfp, "using ReSID emulation");

  } else {
    return FALSE;
  }

  if (builder == NULL) return FALSE;

  if (siddecfp->kernal != NULL) siddecfp->player->setKernal (siddecfp->kernal->data);
  if (siddecfp->basic != NULL) siddecfp->player->setBasic (siddecfp->basic->data);
  if (siddecfp->chargen != NULL) siddecfp->player->setChargen (siddecfp->chargen->data);

  siddecfp->config.sidEmulation = builder;
  siddecfp->player->config (siddecfp->config);
  return TRUE;
}


static void
gst_siddecfp_init (GstSidDecFp * siddecfp)
{
  siddecfp->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_event_function (siddecfp->sinkpad, gst_siddecfp_sink_event);
  gst_pad_set_chain_function (siddecfp->sinkpad, gst_siddecfp_chain);
  gst_element_add_pad (GST_ELEMENT (siddecfp), siddecfp->sinkpad);

  siddecfp->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (siddecfp->srcpad, gst_siddecfp_src_event);
  gst_pad_set_query_function (siddecfp->srcpad, gst_siddecfp_src_query);
  gst_pad_use_fixed_caps (siddecfp->srcpad);
  gst_element_add_pad (GST_ELEMENT (siddecfp), siddecfp->srcpad);

  siddecfp->player = new sidplayfp ();
  siddecfp->tune = new SidTune (0);

  siddecfp->emulation = DEFAULT_EMULATION;    /* emulation */

  /* get default config parameters */
  siddecfp->config = siddecfp->player->config ();

  siddecfp->config.defaultSidModel = DEFAULT_SID_MODEL;         /* sid model */
  siddecfp->config.defaultC64Model = DEFAULT_C64_MODEL;         /* c64 model */
  siddecfp->config.ciaModel = DEFAULT_CIA_MODEL;                /* cia model */
  siddecfp->config.forceSidModel = DEFAULT_FORCE_SID_MODEL;     /* force sid model */
  siddecfp->config.forceC64Model = DEFAULT_FORCE_C64_MODEL;     /* force c64 model */
  siddecfp->config.samplingMethod = DEFAULT_SAMPLING_METHOD;    /* sampling method */

  siddecfp->player->config (siddecfp->config);

  siddecfp->tune_buffer = (guchar *) g_malloc (MAX_SID_TUNE_BUF_SIZE);
  siddecfp->tune_len = 0;
  siddecfp->tune_number = 0;
  siddecfp->total_bytes = 0;
  siddecfp->blocksize = DEFAULT_BLOCKSIZE;

  siddecfp->have_group_id = FALSE;
  siddecfp->group_id = G_MAXUINT;
}

static void
gst_siddecfp_finalize (GObject * object)
{
  GstSidDecFp *siddecfp = GST_SIDDECFP (object);

  if (siddecfp->kernal != NULL) g_byte_array_free (siddecfp->kernal, TRUE);
  if (siddecfp->basic != NULL) g_byte_array_free (siddecfp->basic, TRUE);
  if (siddecfp->chargen != NULL) g_byte_array_free (siddecfp->chargen, TRUE);

  g_free (siddecfp->tune_buffer);

  delete (siddecfp->config.sidEmulation);
  delete (siddecfp->tune);
  delete (siddecfp->player);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_tags (GstSidDecFp * siddecfp)
{
  const SidTuneInfo *info;
  GstTagList *list;
  gint count;
  gchar *info_str;
  gsize bytes_read;
  gsize bytes_written;

  info = siddecfp->tune->getInfo ();
  if (NULL != info) {
    count = info->numberOfInfoStrings ();
    list = gst_tag_list_new_empty ();
    if (count > 0) {
      info_str = g_convert (info->infoString (0), -1, "UTF-8", "ISO-8859-1",
          &bytes_read, &bytes_written, NULL);
      if (info_str != NULL) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_TITLE, info_str, (void *) NULL);
      }
      g_free (info_str);
    }
    if (count > 1) {
      info_str = g_convert (info->infoString (1), -1, "UTF-8", "ISO-8859-1",
          &bytes_read, &bytes_written, NULL);
      if (info_str != NULL) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_ARTIST, info_str, (void *) NULL);
      }
      g_free (info_str);
    }
    if (count > 2) {
      info_str = g_convert (info->infoString (2), -1, "UTF-8", "ISO-8859-1",
          &bytes_read, &bytes_written, NULL);
      if (info_str != NULL) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_COPYRIGHT, info_str, (void *) NULL);
      }
      g_free (info_str);
    }
    gst_pad_push_event (siddecfp->srcpad, gst_event_new_tag (list));
  }
}

static gboolean
siddecfp_negotiate (GstSidDecFp * siddecfp)
{
  GstCaps *allowed;
  GstStructure *structure;
  int rate = 44100;
  int channels = 1;
  GstCaps *caps;
  const gchar *str;
  GstAudioFormat format;
  GstEvent *event;
  gchar *stream_id;

  allowed = gst_pad_get_allowed_caps (siddecfp->srcpad);
  if (!allowed)
    goto nothing_allowed;

  GST_DEBUG_OBJECT (siddecfp, "allowed caps: %" GST_PTR_FORMAT, allowed);

  allowed = gst_caps_normalize (allowed);

  structure = gst_caps_get_structure (allowed, 0);

  str = gst_structure_get_string (structure, "format");
  if (str == NULL)
    goto invalid_format;

  format = gst_audio_format_from_string (str);
  switch (format) {
    case GST_AUDIO_FORMAT_S16:
      break;
    default:
      goto invalid_format;
  }

  gst_structure_get_int (structure, "rate", &rate);
  siddecfp->config.frequency = rate;
  gst_structure_get_int (structure, "channels", &channels);
  siddecfp->config.playback = (channels == 1) ?
      SidConfig::MONO : SidConfig::STEREO;

  stream_id =
      gst_pad_create_stream_id (siddecfp->srcpad, GST_ELEMENT_CAST (siddecfp),
      NULL);

  event = gst_pad_get_sticky_event (siddecfp->sinkpad, GST_EVENT_STREAM_START, 0);
  if (event) {
    if (gst_event_parse_group_id (event, &siddecfp->group_id))
      siddecfp->have_group_id = TRUE;
    else
      siddecfp->have_group_id = FALSE;
    gst_event_unref (event);
  } else if (!siddecfp->have_group_id) {
    siddecfp->have_group_id = TRUE;
    siddecfp->group_id = gst_util_group_id_next ();
  }

  event = gst_event_new_stream_start (stream_id);
  if (siddecfp->have_group_id)
    gst_event_set_group_id (event, siddecfp->group_id);

  gst_pad_push_event (siddecfp->srcpad, event);
  g_free (stream_id);

  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, gst_audio_format_to_string (format),
      "layout", G_TYPE_STRING, "interleaved",
      "rate", G_TYPE_INT, siddecfp->config.frequency,
      "channels", G_TYPE_INT, siddecfp->config.playback, NULL);
  gst_pad_set_caps (siddecfp->srcpad, caps);
  gst_caps_unref (caps);

  gst_caps_unref (allowed);

  siddecfp->player->config (siddecfp->config);

  return TRUE;

  /* ERRORS */
nothing_allowed:
  {
    GST_DEBUG_OBJECT (siddecfp, "could not get allowed caps");
    return FALSE;
  }
invalid_format:
  {
    GST_DEBUG_OBJECT (siddecfp, "invalid audio caps");
    gst_caps_unref (allowed);
    return FALSE;
  }
}

static void
play_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstSidDecFp *siddecfp;
  GstBuffer *out;
  GstMapInfo outmap;
  gint64 value, offset, time = 0;
  GstFormat format;
  guint play_bytes;
  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  out = gst_buffer_new_and_alloc (siddecfp->blocksize);

  gst_buffer_map (out, &outmap, GST_MAP_WRITE);
  play_bytes = siddecfp->player->play ((gint16 *)outmap.data, siddecfp->blocksize/2) * 2;
  gst_buffer_unmap (out, &outmap);

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  if (gst_siddecfp_src_convert (siddecfp->srcpad,
          GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &offset))
    GST_BUFFER_OFFSET (out) = offset;

  /* get current timestamp */
  format = GST_FORMAT_TIME;
  if (gst_siddecfp_src_convert (siddecfp->srcpad,
          GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &time))
    GST_BUFFER_TIMESTAMP (out) = time;

  /* update position and get new timestamp to calculate duration */
  siddecfp->total_bytes += play_bytes;

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  if (gst_siddecfp_src_convert (siddecfp->srcpad,
          GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &value))
    GST_BUFFER_OFFSET_END (out) = value;

  format = GST_FORMAT_TIME;
  if (gst_siddecfp_src_convert (siddecfp->srcpad,
          GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &value))
    GST_BUFFER_DURATION (out) = value - time;

  if ((ret = gst_pad_push (siddecfp->srcpad, out)) != GST_FLOW_OK)
    goto pause;

done:
  gst_object_unref (siddecfp);

  return;

  /* ERRORS */
pause:
  {
    if (ret == GST_FLOW_EOS) {
      /* perform EOS logic, FIXME, segment seek? */
      gst_pad_push_event (pad, gst_event_new_eos ());
    } else if (ret < GST_FLOW_EOS || ret == GST_FLOW_NOT_LINKED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_FLOW_ERROR (siddecfp, ret);
      gst_pad_push_event (pad, gst_event_new_eos ());
    }

    GST_INFO_OBJECT (siddecfp, "pausing task, reason: %s",
        gst_flow_get_name (ret));
    gst_pad_pause_task (pad);
    goto done;
  }
}

static gboolean
start_play_tune (GstSidDecFp * siddecfp)
{
  gboolean res;
  GstSegment segment;

  siddecfp->tune->read (siddecfp->tune_buffer, siddecfp->tune_len);

  if (!siddecfp->tune->selectSong (siddecfp->tune_number))
    goto could_not_select_song;

  if (!siddecfp->player->load(siddecfp->tune))
    goto could_not_load;

  if (!siddecfp_negotiate (siddecfp))
    goto could_not_negotiate;

  if (!create_builder (siddecfp))
    goto could_not_create_builder;

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (siddecfp->srcpad, gst_event_new_segment (&segment));
  siddecfp->total_bytes = 0;
  siddecfp->have_group_id = FALSE;
  siddecfp->group_id = G_MAXUINT;

  res = gst_pad_start_task (siddecfp->srcpad,
      (GstTaskFunction) play_loop, siddecfp->srcpad, NULL);

  update_tags (siddecfp);

  return res;

  /* ERRORS */
could_not_select_song:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT,
        ("Could not select song"), ("Could not select song"));
    return FALSE;
  }
could_not_load:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT,
        ("Could not load tune"), ("Could not load tune"));
    return FALSE;
  }
could_not_negotiate:
  {
    GST_ELEMENT_ERROR (siddecfp, CORE, NEGOTIATION,
        ("Could not negotiate format"), ("Could not negotiate format"));
    return FALSE;
  }
could_not_create_builder:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT,
        ("Could not create builder"), ("Could not create builder"));
    return FALSE;
  }
}

static gboolean
gst_siddecfp_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSidDecFp *siddecfp;
  gboolean res;

  siddecfp = GST_SIDDECFP (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      res = start_play_tune (siddecfp);
      break;
    case GST_EVENT_SEGMENT:
      res = TRUE;
      break;
    default:
      res = TRUE;
      break;
  }
  gst_event_unref (event);

  return res;
}

static GstFlowReturn
gst_siddecfp_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSidDecFp *siddecfp;
  guint64 size;

  siddecfp = GST_SIDDECFP (parent);

  size = gst_buffer_get_size (buffer);
  if (siddecfp->tune_len + size > MAX_SID_TUNE_BUF_SIZE)
    goto overflow;

  gst_buffer_extract (buffer, 0, siddecfp->tune_buffer + siddecfp->tune_len, size);

  siddecfp->tune_len += size;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;

  /* ERRORS */
overflow:
  {
    GST_ELEMENT_ERROR (siddecfp, STREAM, DECODE,
        (NULL), ("Input data bigger than allowed buffer size"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_siddecfp_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  GstSidDecFp *siddecfp;
  gint bytes_per_sample;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  bytes_per_sample =
      (16 >> 3) * siddecfp->config.playback;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * siddecfp->config.frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (siddecfp->config.frequency == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              siddecfp->config.frequency);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value,
              scale * siddecfp->config.frequency, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
gst_siddecfp_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  gst_event_unref (event);

  return res;
}

static gboolean
gst_siddecfp_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstSidDecFp *siddecfp;

  siddecfp = GST_SIDDECFP (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);

      /* we only know about our bytes, convert to requested format */
      res &= gst_siddecfp_src_convert (pad,
          GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &current);
      if (res) {
        gst_query_set_position (query, format, current);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static GByteArray *
copy_byte_array (GByteArray *old, GByteArray *src, gsize expected_size)
{
  GByteArray *ret = NULL;
  if (old != NULL) g_byte_array_free (old, TRUE);
  if (src == NULL) return NULL;
  if (src->len != expected_size) return NULL;
  ret = g_byte_array_new ();
  g_byte_array_append (ret, src->data, src->len);
  return ret;
}

static void
gst_siddecfp_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSidDecFp *siddecfp = GST_SIDDECFP (object);

  switch (prop_id) {
    case PROP_EMULATION:
      siddecfp->emulation = (SidDecFpEmulation)g_value_get_enum (value);
      break;
    case PROP_TUNE:
      siddecfp->tune_number = g_value_get_int (value);
      break;
    case PROP_FILTER:
      siddecfp->filter = g_value_get_boolean (value);
      break;
    case PROP_C64_MODEL:
      siddecfp->config.defaultC64Model = (SidConfig::c64_model_t)g_value_get_enum (value);
      break;
    case PROP_SID_MODEL:
      siddecfp->config.defaultSidModel = (SidConfig::sid_model_t)g_value_get_enum (value);
      break;
    case PROP_CIA_MODEL:
      siddecfp->config.ciaModel = (SidConfig::cia_model_t)g_value_get_enum (value);
      break;
    case PROP_FORCE_SID_MODEL:
      siddecfp->config.forceSidModel = g_value_get_boolean (value);
      break;
    case PROP_FORCE_C64_MODEL:
      siddecfp->config.forceC64Model = g_value_get_boolean (value);
      break;
    case PROP_SAMPLING_METHOD:
      siddecfp->config.samplingMethod = (SidConfig::sampling_method_t)g_value_get_enum (value);
      break;
    case PROP_DIGI_BOOST:
      siddecfp->config.digiBoost = g_value_get_boolean (value);
      break;
    case PROP_FILTER_CURVE_6581:
      siddecfp->filter_curve_6581 = g_value_get_double (value);
      break;
    case PROP_FILTER_CURVE_8580:
      siddecfp->filter_curve_8580 = g_value_get_double (value);
      break;
    case PROP_FILTER_BIAS:
      siddecfp->filter_bias = g_value_get_double (value);
      break;
    case PROP_KERNAL:
      siddecfp->kernal = copy_byte_array (siddecfp->kernal, (GByteArray *)g_value_get_boxed (value), 8192);
      break;
    case PROP_BASIC:
      siddecfp->basic = copy_byte_array (siddecfp->basic, (GByteArray *)g_value_get_boxed (value), 8192);
      break;
    case PROP_CHARGEN:
      siddecfp->chargen = copy_byte_array (siddecfp->chargen, (GByteArray *)g_value_get_boxed (value), 4096);
      break;
    case PROP_BLOCKSIZE:
      siddecfp->blocksize = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
  siddecfp->player->config (siddecfp->config);
}

static void
gst_siddecfp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSidDecFp *siddecfp = GST_SIDDECFP (object);

  switch (prop_id) {
    case PROP_EMULATION:
      g_value_set_enum (value, siddecfp->emulation);
      break;
    case PROP_TUNE:
      g_value_set_int (value, siddecfp->tune_number);
      break;
    case PROP_N_TUNES: {
      g_value_set_int (value, 0);
      if (siddecfp->tune != NULL) {
        const SidTuneInfo *info = siddecfp->tune->getInfo();
        if (info != NULL)
          g_value_set_int (value, info->songs ());
      }
      break;
    }
    case PROP_FILTER:
      g_value_set_boolean (value, siddecfp->filter);
      break;
    case PROP_C64_MODEL:
      g_value_set_enum (value, siddecfp->config.defaultC64Model);
      break;
    case PROP_SID_MODEL:
      g_value_set_enum (value, siddecfp->config.defaultSidModel);
      break;
    case PROP_CIA_MODEL:
      g_value_set_enum (value, siddecfp->config.ciaModel);
      break;
    case PROP_FORCE_SID_MODEL:
      g_value_set_boolean (value, siddecfp->config.forceSidModel);
      break;
    case PROP_FORCE_C64_MODEL:
      g_value_set_boolean (value, siddecfp->config.forceC64Model);
      break;
    case PROP_SAMPLING_METHOD:
      g_value_set_enum (value, siddecfp->config.samplingMethod);
      break;
    case PROP_DIGI_BOOST:
      g_value_set_boolean (value, siddecfp->config.digiBoost);
      break;
    case PROP_FILTER_CURVE_6581:
      g_value_set_double (value, siddecfp->filter_curve_6581);
      break;
    case PROP_FILTER_CURVE_8580:
      g_value_set_double (value, siddecfp->filter_curve_8580);
      break;
    case PROP_FILTER_BIAS:
      g_value_set_double (value, siddecfp->filter_bias);
      break;
    case PROP_BLOCKSIZE:
      g_value_set_uint (value, siddecfp->blocksize);
      break;
    case PROP_METADATA:
      g_value_set_boxed (value, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "siddecfp", 257, /*GST_RANK_PRIMARY,*/
      GST_TYPE_SIDDECFP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sidfp,
    "Uses libsidplayfp to decode .sid files",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
