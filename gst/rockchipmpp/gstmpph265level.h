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

#ifndef  __GST_MPP_H265_LEVEL_H__
#define  __GST_MPP_H265_LEVEL_H__

#include <stdint.h>

#define GST_MPP_H265_TIER_AUTO -1
#define GST_MPP_H265_TIER_MAIN 0
#define GST_MPP_H265_TIER_HIGH 1

typedef struct
{
  const char *name;             /* "5.1" */
  int level_idc;                /* general_level_idc, 30 x level: 153 */
  uint64_t max_luma_ps;         /* MaxLumaPs, samples (Table A.8) */
  uint64_t max_luma_sr;         /* MaxLumaSr, samples/s (Table A.9) */
  uint32_t max_br_main;         /* MaxBR, Main tier, 1000 bit/s (Table A.9) */
  uint32_t max_br_high;         /* MaxBR, High tier; 0: the level has none */
} GstMppH265Level;

const GstMppH265Level *gst_mpp_h265_level_from_idc (int level_idc);
const GstMppH265Level *gst_mpp_h265_level_from_name (const char *name);

int gst_mpp_h265_level_size_floor (unsigned width, unsigned height);

int gst_mpp_h265_level_holds (const GstMppH265Level * level, int tier,
    unsigned width, unsigned height, unsigned fps_n, unsigned fps_d,
    uint64_t max_bps);

int gst_mpp_h265_level_default_tier (const GstMppH265Level * level);

int gst_mpp_h265_level_choose (unsigned width, unsigned height,
    unsigned fps_n, unsigned fps_d, uint64_t max_bps, int tier,
    int *tier_out);

#endif /* __GST_MPP_H265_LEVEL_H__ */
