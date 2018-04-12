/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include "config.h"

#include <glib.h>
#include <libmogwai-tariff/period.h>

#include "common.h"


void
assert_periods_equal (MwtPeriod *period1,
                      MwtPeriod *period2)
{
  g_assert_true (MWT_IS_PERIOD (period1));
  g_assert_true (MWT_IS_PERIOD (period2));

  GDateTime *period1_start = mwt_period_get_start (period1);
  GTimeZone *period1_start_tz = g_date_time_get_timezone (period1_start);
  GDateTime *period1_end = mwt_period_get_end (period1);
  GTimeZone *period1_end_tz = g_date_time_get_timezone (period1_end);
  GDateTime *period2_start = mwt_period_get_start (period2);
  GTimeZone *period2_start_tz = g_date_time_get_timezone (period2_start);
  GDateTime *period2_end = mwt_period_get_end (period2);
  GTimeZone *period2_end_tz = g_date_time_get_timezone (period2_end);

  g_assert_true (g_date_time_equal (period1_start, period2_start));
  g_assert_cmpstr (g_time_zone_get_identifier (period1_start_tz), ==,
                   g_time_zone_get_identifier (period2_start_tz));
  g_assert_true (g_date_time_equal (period1_end, period2_end));
  g_assert_cmpstr (g_time_zone_get_identifier (period1_end_tz), ==,
                   g_time_zone_get_identifier (period2_end_tz));

  g_assert_cmpint (mwt_period_get_repeat_type (period1), ==,
                   mwt_period_get_repeat_type (period2));
  g_assert_cmpuint (mwt_period_get_repeat_period (period1), ==,
                    mwt_period_get_repeat_period (period2));

  g_assert_cmpuint (mwt_period_get_capacity_limit (period1), ==,
                    mwt_period_get_capacity_limit (period2));
}
