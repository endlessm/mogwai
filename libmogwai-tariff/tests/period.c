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
#include <gio/gio.h>
#include <libmogwai-tariff/period.h>
#include <locale.h>


static void
assert_period_validate (GDateTime           *start,
                        GDateTime           *end,
                        MwtPeriodRepeatType  repeat_type,
                        guint                repeat_period)
{
  g_autoptr(GError) error = NULL;

  gboolean retval = mwt_period_validate (start, end, repeat_type, repeat_period, &error);
  g_assert_error (error, MWT_PERIOD_ERROR, MWT_PERIOD_ERROR_INVALID);
  g_assert_false (retval);
}

/* Test constructing a #MwtPeriod object with invalid arguments. */
static void
test_period_validation (void)
{
  g_autoptr(GDateTime) start = g_date_time_new_utc (2000, 01, 01, 01, 01, 01);
  g_autoptr(GDateTime) end = g_date_time_new_utc (2000, 02, 01, 01, 01, 01);

  assert_period_validate (NULL, end, MWT_PERIOD_REPEAT_HOUR, 1);
  assert_period_validate (start, NULL, MWT_PERIOD_REPEAT_HOUR, 1);
  assert_period_validate (end, start, MWT_PERIOD_REPEAT_HOUR, 1);
  assert_period_validate (start, end, MWT_PERIOD_REPEAT_NONE, 1);
  assert_period_validate (start, end, MWT_PERIOD_REPEAT_HOUR, 0);
  assert_period_validate (start, end, 500, 1);
}

/* Test the GObject properties on a period. */
static void
test_period_properties (void)
{
  g_autoptr(GDateTime) start = g_date_time_new_utc (2000, 01, 01, 01, 01, 01);
  g_autoptr(GDateTime) end = g_date_time_new_utc (2000, 02, 01, 01, 01, 01);

  g_autoptr(MwtPeriod) period = NULL;
  period = mwt_period_new (start, end, MWT_PERIOD_REPEAT_HOUR, 1,
                           "capacity-limit", (guint64) 5671,
                           NULL);
  g_assert_true (MWT_IS_PERIOD (period));

  g_assert_true (g_date_time_equal (mwt_period_get_start (period), start));
  g_assert_true (g_date_time_equal (mwt_period_get_end (period), end));
  g_assert_cmpint (mwt_period_get_repeat_type (period), ==, MWT_PERIOD_REPEAT_HOUR);
  g_assert_cmpuint (mwt_period_get_repeat_period (period), ==, 1);
  g_assert_cmpuint (mwt_period_get_capacity_limit (period), ==, 5671);

  g_autoptr(GDateTime) actual_start = NULL;
  g_autoptr(GDateTime) actual_end = NULL;
  MwtPeriodRepeatType repeat_type;
  guint repeat_period;
  guint64 capacity_limit;

  g_object_get (G_OBJECT (period),
                "start", &actual_start,
                "end", &actual_end,
                "repeat-type", &repeat_type,
                "repeat-period", &repeat_period,
                "capacity-limit", &capacity_limit,
                NULL);

  g_assert_true (g_date_time_equal (actual_start, start));
  g_assert_true (g_date_time_equal (actual_end, end));
  g_assert_cmpint (repeat_type, ==, MWT_PERIOD_REPEAT_HOUR);
  g_assert_cmpuint (repeat_period, ==, 1);
  g_assert_cmpuint (capacity_limit, ==, 5671);
}

/* Test that the default values of all the limit properties for a period are
 * sensible. */
static void
test_period_properties_defaults (void)
{
  g_autoptr(GDateTime) start = g_date_time_new_utc (2000, 01, 01, 01, 01, 01);
  g_autoptr(GDateTime) end = g_date_time_new_utc (2000, 02, 01, 01, 01, 01);

  g_autoptr(MwtPeriod) period = NULL;
  period = mwt_period_new (start, end, MWT_PERIOD_REPEAT_NONE, 0,
                           /* leave everything as default */
                           NULL);
  g_assert_true (MWT_IS_PERIOD (period));

  /* Test default property values. */
  g_assert_cmpuint (mwt_period_get_capacity_limit (period), ==, G_MAXUINT64);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/period/validation", test_period_validation);
  g_test_add_func ("/period/properties", test_period_properties);
  g_test_add_func ("/period/properties/defaults", test_period_properties_defaults);

  return g_test_run ();
}
