/*
 * Copyright 2021 Rockchip Electronics Co., Ltd
 *     Author: Jeffy Chen <jeffy.chen@rock-chips.com>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpph265enc.h"
#include "gstmpph265level.h"

/* FIXME: Not all chips support NV24 and Y444. */
#define MPP_H265_ENC_FORMATS MPP_ENC_FORMATS ", NV24, Y444"

#define GST_MPP_H265_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    GST_TYPE_MPP_H265_ENC, GstMppH265Enc))

#define GST_CAT_DEFAULT mpp_h265_enc_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstMppH265Enc
{
  GstMppEnc parent;

  guint qp_init;
  guint qp_min;
  guint qp_max;
  guint qp_min_i;
  guint qp_max_i;
  gint qp_ip;

  gint level;
  gint tier;

  /* what the stream gets: level_idc and tier */
  gint out_level;
  gint out_tier;
};

#define parent_class gst_mpp_h265_enc_parent_class
G_DEFINE_TYPE (GstMppH265Enc, gst_mpp_h265_enc, GST_TYPE_MPP_ENC);

#define DEFAULT_PROP_QP_INIT 26
#define DEFAULT_PROP_QP_MIN 0   /* Auto */
#define DEFAULT_PROP_QP_MAX 0   /* Auto */
#define DEFAULT_PROP_QP_MIN_I 0 /* Auto */
#define DEFAULT_PROP_QP_MAX_I 0 /* Auto */
#define DEFAULT_PROP_QP_IP -1   /* Auto */
#define DEFAULT_PROP_LEVEL 0    /* Auto */
/* Bit rate a second of the stream may reach, against the rate control's
 * maximum: 3/2 */
#define PEAK_HEADROOM_NUM 3
#define PEAK_HEADROOM_DEN 2
#define DEFAULT_PROP_TIER GST_MPP_H265_TIER_AUTO

/* general_profile_idc of Main; MPP's own define is not in its public headers */
#define GST_MPP_H265_PROFILE_MAIN 1

enum
{
  PROP_0,
  PROP_QP_INIT,
  PROP_QP_MIN,
  PROP_QP_MAX,
  PROP_QP_MIN_I,
  PROP_QP_MAX_I,
  PROP_QP_IP,
  PROP_LEVEL,
  PROP_TIER,
  PROP_LAST,
};

#define GST_MPP_H265_ENC_SIZE_CAPS \
    "width  = (int) [ 96, MAX ], height = (int) [ 64, MAX ]"

static GstStaticPadTemplate gst_mpp_h265_enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        GST_MPP_H265_ENC_SIZE_CAPS ","
        "stream-format = (string) { byte-stream }, "
        "alignment = (string) { au }")
    );

static GstStaticPadTemplate gst_mpp_h265_enc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,"
        "format = (string) { " MPP_H265_ENC_FORMATS " }, "
        GST_MPP_H265_ENC_SIZE_CAPS ";"));

#define GST_TYPE_MPP_H265_ENC_LEVEL (gst_mpp_h265_enc_level_get_type ())
static GType
gst_mpp_h265_enc_level_get_type (void)
{
  static GType level = 0;

  if (!level) {
    static const GEnumValue levels[] = {
      {0, "Auto", "auto"},
      {30, "1", "1"},
      {60, "2", "2"},
      {63, "2.1", "2.1"},
      {90, "3", "3"},
      {93, "3.1", "3.1"},
      {120, "4", "4"},
      {123, "4.1", "4.1"},
      {150, "5", "5"},
      {153, "5.1", "5.1"},
      {156, "5.2", "5.2"},
      {180, "6", "6"},
      {183, "6.1", "6.1"},
      {186, "6.2", "6.2"},
      {0, NULL, NULL},
    };
    level = g_enum_register_static ("GstMppH265Level", levels);
  }
  return level;
}

#define GST_TYPE_MPP_H265_ENC_TIER (gst_mpp_h265_enc_tier_get_type ())
static GType
gst_mpp_h265_enc_tier_get_type (void)
{
  static GType tier = 0;

  if (!tier) {
    static const GEnumValue tiers[] = {
      {GST_MPP_H265_TIER_AUTO, "Auto", "auto"},
      {GST_MPP_H265_TIER_MAIN, "Main", "main"},
      {GST_MPP_H265_TIER_HIGH, "High", "high"},
      {0, NULL, NULL},
    };
    tier = g_enum_register_static ("GstMppH265Tier", tiers);
  }
  return tier;
}

static void
gst_mpp_h265_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (object);
  GstMppH265Enc *self = GST_MPP_H265_ENC (encoder);
  GstMppEnc *mppenc = GST_MPP_ENC (encoder);

  switch (prop_id) {
    case PROP_QP_INIT:{
      guint qp_init = g_value_get_uint (value);
      if (self->qp_init == qp_init)
        return;

      self->qp_init = qp_init;
      break;
    }
    case PROP_QP_MIN:{
      guint qp_min = g_value_get_uint (value);
      if (self->qp_min == qp_min)
        return;

      self->qp_min = qp_min;
      break;
    }
    case PROP_QP_MAX:{
      guint qp_max = g_value_get_uint (value);
      if (self->qp_max == qp_max)
        return;

      self->qp_max = qp_max;
      break;
    }
    case PROP_QP_MIN_I:{
      guint qp_min_i = g_value_get_uint (value);
      if (self->qp_min_i == qp_min_i)
        return;

      self->qp_min_i = qp_min_i;
      break;
    }
    case PROP_QP_MAX_I:{
      guint qp_max_i = g_value_get_uint (value);
      if (self->qp_max_i == qp_max_i)
        return;

      self->qp_max_i = qp_max_i;
      break;
    }
    case PROP_QP_IP:{
      gint qp_ip = g_value_get_int (value);
      if (self->qp_ip == qp_ip)
        return;

      self->qp_ip = qp_ip;
      break;
    }
    case PROP_LEVEL:{
      gint level = g_value_get_enum (value);
      if (self->level == level)
        return;

      self->level = level;
      break;
    }
    case PROP_TIER:{
      gint tier = g_value_get_enum (value);
      if (self->tier == tier)
        return;

      self->tier = tier;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }

  mppenc->prop_dirty = TRUE;
}

static void
gst_mpp_h265_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (object);
  GstMppH265Enc *self = GST_MPP_H265_ENC (encoder);

  switch (prop_id) {
    case PROP_QP_INIT:
      g_value_set_uint (value, self->qp_init);
      break;
    case PROP_QP_MIN:
      g_value_set_uint (value, self->qp_min);
      break;
    case PROP_QP_MAX:
      g_value_set_uint (value, self->qp_max);
      break;
    case PROP_QP_MIN_I:
      g_value_set_uint (value, self->qp_min_i);
      break;
    case PROP_QP_MAX_I:
      g_value_set_uint (value, self->qp_max_i);
      break;
    case PROP_QP_IP:
      g_value_set_int (value, self->qp_ip);
      break;
    case PROP_LEVEL:
      g_value_set_enum (value, self->level);
      break;
    case PROP_TIER:
      g_value_set_enum (value, self->tier);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_mpp_h265_enc_set_src_caps (GstVideoEncoder * encoder)
{
  GstMppH265Enc *self = GST_MPP_H265_ENC (encoder);
  const GstMppH265Level *level;
  GstStructure *structure;
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("video/x-h265");

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "stream-format",
      G_TYPE_STRING, "byte-stream", NULL);
  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);
  gst_structure_set (structure, "profile", G_TYPE_STRING, "main", NULL);

  level = gst_mpp_h265_level_from_idc (self->out_level);
  if (level) {
    gst_structure_set (structure, "tier", G_TYPE_STRING,
        self->out_tier == GST_MPP_H265_TIER_HIGH ? "high" : "main", NULL);
    gst_structure_set (structure, "level", G_TYPE_STRING, level->name, NULL);
  }

  return gst_mpp_enc_set_src_caps (encoder, caps);
}

/* A fixed level or tier string in the first structure of the downstream caps */
static void
gst_mpp_h265_enc_downstream_level (GstVideoEncoder * encoder,
    const GstMppH265Level ** level, gint * tier)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *string;

  caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!caps)
    return;

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    gst_caps_unref (caps);
    return;
  }

  structure = gst_caps_get_structure (caps, 0);

  string = gst_structure_get_string (structure, "level");
  if (!*level && string) {
    *level = gst_mpp_h265_level_from_name (string);
    if (!*level)
      GST_WARNING_OBJECT (encoder, "unknown level %s downstream", string);
  }

  string = gst_structure_get_string (structure, "tier");
  if (*tier == GST_MPP_H265_TIER_AUTO && string) {
    if (!g_strcmp0 (string, "main"))
      *tier = GST_MPP_H265_TIER_MAIN;
    else if (!g_strcmp0 (string, "high"))
      *tier = GST_MPP_H265_TIER_HIGH;
    else
      GST_WARNING_OBJECT (encoder, "unknown tier %s downstream", string);
  }

  gst_caps_unref (caps);
}

/*
 * The level and tier the stream gets: each from its property, else from the
 * downstream caps, else the picture size's floor (up to 1080p: 4.1 High; up
 * to 4096x2176: 5.1 High) or the next level up whose limits (H.265 Annex A)
 * hold the luma sample rate and the bit rate the rate control may reach.
 */
static void
gst_mpp_h265_enc_resolve_level (GstVideoEncoder * encoder)
{
  GstMppH265Enc *self = GST_MPP_H265_ENC (encoder);
  GstMppEnc *mppenc = GST_MPP_ENC (encoder);
  GstVideoInfo *info = &mppenc->info;
  guint width = GST_VIDEO_INFO_WIDTH (info);
  guint height = GST_VIDEO_INFO_HEIGHT (info);
  guint fps_n = GST_VIDEO_INFO_FPS_N (info);
  guint fps_d = GST_VIDEO_INFO_FPS_D (info);
  const GstMppH265Level *level = gst_mpp_h265_level_from_idc (self->level);
  gint tier = self->tier;
  guint64 max_bps = 0;
  gint level_idc;

  /* The bound gst_mpp_enc_apply_properties gives the rate control (none with
   * fixed QP), with headroom: MPP holds it over its statistics window, not
   * per second, and a second of a VBR stream reached 1.29 times it. */
  if (mppenc->rc_mode != MPP_ENC_RC_MODE_FIXQP) {
    guint64 bps = mppenc->bps;

    if (!bps && fps_d)
      bps = (guint64) width * height / 8 * (fps_n / fps_d);
    if (bps)
      max_bps = (mppenc->bps_max ? mppenc->bps_max : bps * 17 / 16) *
          PEAK_HEADROOM_NUM / PEAK_HEADROOM_DEN;
  }

  if (!level || tier == GST_MPP_H265_TIER_AUTO)
    gst_mpp_h265_enc_downstream_level (encoder, &level, &tier);

  if (level) {
    if (tier == GST_MPP_H265_TIER_AUTO)
      tier = gst_mpp_h265_level_default_tier (level);
  } else {
    level_idc = gst_mpp_h265_level_choose (width, height, fps_n, fps_d,
        max_bps, tier, &tier);
    if (!level_idc) {
      GST_WARNING_OBJECT (self, "no level holds %ux%u at %u/%u fps and %"
          G_GUINT64_FORMAT " bit/s; writing 6.2", width, height, fps_n,
          fps_d, max_bps);
      level_idc = 186;
      if (tier == GST_MPP_H265_TIER_AUTO)
        tier = GST_MPP_H265_TIER_HIGH;
    }
    level = gst_mpp_h265_level_from_idc (level_idc);
  }

  /* MPP never writes a level below the one the picture size needs */
  level_idc = gst_mpp_h265_level_size_floor (width, height);
  if (level->level_idc < level_idc) {
    GST_WARNING_OBJECT (self, "level %s is too low for %ux%u; MPP writes %s",
        level->name, width, height,
        gst_mpp_h265_level_from_idc (level_idc)->name);
    level = gst_mpp_h265_level_from_idc (level_idc);
  }

  /* High tier exists from level 4; MPP writes Main below it */
  if (tier == GST_MPP_H265_TIER_HIGH && !level->max_br_high) {
    GST_WARNING_OBJECT (self, "level %s has no High tier; writing Main",
        level->name);
    tier = GST_MPP_H265_TIER_MAIN;
  }

  if (!gst_mpp_h265_level_holds (level, tier, width, height, fps_n, fps_d,
          max_bps))
    GST_WARNING_OBJECT (self, "level %s %s tier does not hold %ux%u at %u/%u "
        "fps and up to %" G_GUINT64_FORMAT " bit/s", level->name,
        tier == GST_MPP_H265_TIER_HIGH ? "High" : "Main", width, height,
        fps_n, fps_d, max_bps);

  self->out_level = level->level_idc;
  self->out_tier = tier;

  GST_INFO_OBJECT (self, "level %s, %s tier", level->name,
      tier == GST_MPP_H265_TIER_HIGH ? "High" : "Main");
}

static gboolean
gst_mpp_h265_enc_apply_properties (GstVideoEncoder * encoder)
{
  GstMppH265Enc *self = GST_MPP_H265_ENC (encoder);
  GstMppEnc *mppenc = GST_MPP_ENC (encoder);

  if (G_LIKELY (!mppenc->prop_dirty))
    return TRUE;

  mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_init", self->qp_init);

  if (mppenc->rc_mode == MPP_ENC_RC_MODE_FIXQP) {
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_min", self->qp_init);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_max", self->qp_init);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_min_i", self->qp_init);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_max_i", self->qp_init);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_ip", 0);
  } else {
    /* MPP_ENC_RC_MODE_CBR/MPP_ENC_RC_MODE_VBR/MPP_ENC_RC_MODE_AVBR */
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_min",
        self->qp_min ? self->qp_min : 10);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_max",
        self->qp_max ? self->qp_max : 51);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_min_i",
        self->qp_min_i ? self->qp_min_i : 10);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_max_i",
        self->qp_max_i ? self->qp_max_i : 51);
    mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "rc:qp_ip",
        self->qp_ip >= 0 ? self->qp_ip : 2);
  }

  gst_mpp_h265_enc_resolve_level (encoder);
  mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "h265:profile",
      GST_MPP_H265_PROFILE_MAIN);
  mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "h265:level", self->out_level);
  mpp_enc_cfg_set_s32 (mppenc->mpp_cfg, "h265:tier", self->out_tier);

  if (!gst_mpp_enc_apply_properties (encoder))
    return FALSE;

  return gst_mpp_h265_enc_set_src_caps (encoder);
}

static gboolean
gst_mpp_h265_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS (parent_class);

  if (!pclass->set_format (encoder, state))
    return FALSE;

  return gst_mpp_h265_enc_apply_properties (encoder);
}

static GstFlowReturn
gst_mpp_h265_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS (parent_class);

  if (G_UNLIKELY (!gst_mpp_h265_enc_apply_properties (encoder))) {
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return pclass->handle_frame (encoder, frame);
}

static void
gst_mpp_h265_enc_init (GstMppH265Enc * self)
{
  self->parent.mpp_type = MPP_VIDEO_CodingHEVC;

  self->qp_init = DEFAULT_PROP_QP_INIT;
  self->qp_min = DEFAULT_PROP_QP_MIN;
  self->qp_max = DEFAULT_PROP_QP_MAX;
  self->qp_min_i = DEFAULT_PROP_QP_MIN_I;
  self->qp_max_i = DEFAULT_PROP_QP_MAX_I;
  self->qp_ip = DEFAULT_PROP_QP_IP;
  self->level = DEFAULT_PROP_LEVEL;
  self->tier = DEFAULT_PROP_TIER;
}

static void
gst_mpp_h265_enc_class_init (GstMppH265EncClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "mpph265enc", 0,
      "MPP H265 encoder");

  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_mpp_h265_enc_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mpp_h265_enc_handle_frame);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_mpp_h265_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_mpp_h265_enc_get_property);

  g_object_class_install_property (gobject_class, PROP_QP_INIT,
      g_param_spec_uint ("qp-init", "Initial QP",
          "Initial QP (lower value means higher quality)",
          0, 51, DEFAULT_PROP_QP_INIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MIN,
      g_param_spec_uint ("qp-min", "Min QP",
          "Min QP (0 = default)", 0, 51, DEFAULT_PROP_QP_MIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MAX,
      g_param_spec_uint ("qp-max", "Max QP",
          "Max QP (0 = default)", 0, 51, DEFAULT_PROP_QP_MAX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MIN_I,
      g_param_spec_uint ("qp-min-i", "Min Intra QP",
          "Min Intra QP (0 = default)", 0, 51, DEFAULT_PROP_QP_MIN_I,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MAX_I,
      g_param_spec_uint ("qp-max-i", "Max Intra QP",
          "Max Intra QP (0 = default)", 0, 51, DEFAULT_PROP_QP_MAX_I,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_IP,
      g_param_spec_int ("qp-delta-ip", "Delta QP between I and P",
          "Delta QP between I and P (-1 = default)", -1, 8, DEFAULT_PROP_QP_IP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_enum ("level", "H265 level",
          "H265 level (auto = from the downstream caps, else 4.1 up to 1080p, "
          "5.1 up to 4096x2176, or higher when the frame rate or bit rate "
          "needs it)",
          GST_TYPE_MPP_H265_ENC_LEVEL, DEFAULT_PROP_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIER,
      g_param_spec_enum ("tier", "H265 tier",
          "H265 tier (auto = from the downstream caps, else High from level 4)",
          GST_TYPE_MPP_H265_ENC_TIER, DEFAULT_PROP_TIER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mpp_h265_enc_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mpp_h265_enc_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "Rockchip Mpp H265 Encoder", "Codec/Encoder/Video",
      "Encode video streams via Rockchip Mpp",
      "Jeffy Chen <jeffy.chen@rock-chips.com>");
}

gboolean
gst_mpp_h265_enc_register (GstPlugin * plugin, guint rank)
{
  if (!gst_mpp_enc_supported (MPP_VIDEO_CodingHEVC))
    return FALSE;

  return gst_element_register (plugin, "mpph265enc", rank,
      gst_mpp_h265_enc_get_type ());
}
