/*
 * H.265 levels and tiers: the limits of Annex A and the choice of a level
 * for a stream. Plain C, no GStreamer types, so it can be tested alone.
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

#include <stddef.h>
#include <string.h>

#include "gstmpph265level.h"

/* MPP pads the coded picture to a multiple of its smallest CU, 8 samples */
#define CODED(x) (((uint64_t) (x) + 7) & ~(uint64_t) 7)

/*
 * ITU-T H.265 Tables A.8 and A.9. MaxBR is in units of 1000 bit/s, the VCL
 * factor of the Main and Main 10 profiles; comparing a whole stream's rate
 * with it leaves the NAL overhead as margin.
 */
static const GstMppH265Level levels[] = {
  {"1", 30, 36864, 552960, 128, 0},
  {"2", 60, 122880, 3686400, 1500, 0},
  {"2.1", 63, 245760, 7372800, 3000, 0},
  {"3", 90, 552960, 16588800, 6000, 0},
  {"3.1", 93, 983040, 33177600, 10000, 0},
  {"4", 120, 2228224, 66846720, 12000, 30000},
  {"4.1", 123, 2228224, 133693440, 20000, 50000},
  {"5", 150, 8912896, 267386880, 25000, 100000},
  {"5.1", 153, 8912896, 534773760, 40000, 160000},
  {"5.2", 156, 8912896, 1069547520, 60000, 240000},
  {"6", 180, 35651584, 1069547520, 60000, 240000},
  {"6.1", 183, 35651584, 2139095040, 120000, 480000},
  {"6.2", 186, 35651584, 4278190080U, 240000, 800000},
};

#define N_LEVELS (sizeof (levels) / sizeof (levels[0]))

const GstMppH265Level *
gst_mpp_h265_level_from_idc (int level_idc)
{
  size_t i;

  for (i = 0; i < N_LEVELS; i++)
    if (levels[i].level_idc == level_idc)
      return &levels[i];

  return NULL;
}

const GstMppH265Level *
gst_mpp_h265_level_from_name (const char *name)
{
  size_t i;

  if (!name)
    return NULL;

  for (i = 0; i < N_LEVELS; i++)
    if (!strcmp (levels[i].name, name))
      return &levels[i];

  return NULL;
}

/*
 * The level MPP writes at least, whatever it is asked for: the lowest whose
 * MaxLumaPs holds width x height (h265e_set_vps).
 */
int
gst_mpp_h265_level_size_floor (unsigned width, unsigned height)
{
  uint64_t ps = (uint64_t) width * height;
  size_t i;

  for (i = 0; i < N_LEVELS; i++)
    if (levels[i].max_luma_ps >= ps)
      return levels[i].level_idc;

  return levels[N_LEVELS - 1].level_idc;
}

/*
 * Whether a level and tier hold a stream: picture size, width and height
 * (at most sqrt (8 x MaxLumaPs) each), luma sample rate and bit rate.
 * fps_n 0: rate unknown, not checked. max_bps 0: no bound, not checked.
 */
int
gst_mpp_h265_level_holds (const GstMppH265Level * level, int tier,
    unsigned width, unsigned height, unsigned fps_n, unsigned fps_d,
    uint64_t max_bps)
{
  uint64_t w = CODED (width), h = CODED (height);
  uint64_t max_br;

  if (w * h > level->max_luma_ps || w * w > 8 * level->max_luma_ps ||
      h * h > 8 * level->max_luma_ps)
    return 0;

  if (fps_n && fps_d && w * h * fps_n > level->max_luma_sr * fps_d)
    return 0;

  if (tier == GST_MPP_H265_TIER_HIGH && !level->max_br_high)
    return 0;

  max_br = tier == GST_MPP_H265_TIER_HIGH ?
      level->max_br_high : level->max_br_main;

  return !max_bps || max_bps <= max_br * 1000;
}

/*
 * The lowest level each picture size gets, High tier: what decoders for that
 * size are built for (1080p HEVC deliveries: 4.1 High, 50 Mb/s; UHD Blu-ray:
 * 5.1 High, 160 Mb/s). Smaller pictures start at the lowest level that holds
 * them.
 */
static const struct
{
  uint64_t max_luma_ps;
  int level_idc;
} floors[] = {
  {2228224, 123},
  {8912896, 153},
  {35651584, 183},
};

#define N_FLOORS (sizeof (floors) / sizeof (floors[0]))

/* The tier a level gets when none is asked for: High where it has one */
int
gst_mpp_h265_level_default_tier (const GstMppH265Level * level)
{
  return level->max_br_high ? GST_MPP_H265_TIER_HIGH : GST_MPP_H265_TIER_MAIN;
}

/*
 * The level for a stream: its picture size's floor, or the next level up
 * that holds it (luma sample rate, bit rate) at the tier asked for, else at
 * the level's default tier.
 * Returns the level_idc, or 0 when no level holds it.
 */
int
gst_mpp_h265_level_choose (unsigned width, unsigned height,
    unsigned fps_n, unsigned fps_d, uint64_t max_bps, int tier, int *tier_out)
{
  size_t i, start = N_LEVELS;

  for (i = 0; i < N_LEVELS && start == N_LEVELS; i++)
    if (gst_mpp_h265_level_holds (&levels[i], GST_MPP_H265_TIER_MAIN, width,
            height, 0, 0, 0))
      start = i;

  if (start == N_LEVELS)
    return 0;

  for (i = 0; i < N_FLOORS; i++)
    if (floors[i].max_luma_ps == levels[start].max_luma_ps)
      while (levels[start].level_idc < floors[i].level_idc)
        start++;

  for (i = start; i < N_LEVELS; i++) {
    int t = tier == GST_MPP_H265_TIER_AUTO ?
        gst_mpp_h265_level_default_tier (&levels[i]) : tier;

    if (gst_mpp_h265_level_holds (&levels[i], t, width, height, fps_n, fps_d,
            max_bps)) {
      if (tier_out)
        *tier_out = t;
      return levels[i].level_idc;
    }
  }

  return 0;
}
