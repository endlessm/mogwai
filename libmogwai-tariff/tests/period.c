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

/* Test mwt_period_contains_time() returns correct results for a variety of
 * situations. This tests mwt_period_get_next_recurrence() at the same time,
 * since their results are quite tightly linked. */
static void
test_period_contains_time (void)
{
  const struct
    {
      gint start_year;
      gint start_month;
      gint start_day;
      gint start_hour;
      gint start_minute;
      gdouble start_seconds;
      const gchar *start_tz;

      gint end_year;
      gint end_month;
      gint end_day;
      gint end_hour;
      gint end_minute;
      gdouble end_seconds;
      const gchar *end_tz;

      MwtPeriodRepeatType repeat_type;
      guint repeat_period;

      gint when_year;
      gint when_month;
      gint when_day;
      gint when_hour;
      gint when_minute;
      gdouble when_seconds;
      const gchar *when_tz;

      gboolean expected_contains;

      gint expected_contains_start_year;
      gint expected_contains_start_month;
      gint expected_contains_start_day;
      gint expected_contains_start_hour;
      gint expected_contains_start_minute;
      gdouble expected_contains_start_seconds;
      const gchar *expected_contains_start_tz;

      gint expected_contains_end_year;
      gint expected_contains_end_month;
      gint expected_contains_end_day;
      gint expected_contains_end_hour;
      gint expected_contains_end_minute;
      gdouble expected_contains_end_seconds;
      const gchar *expected_contains_end_tz;

      gboolean expected_next;

      gint expected_next_start_year;
      gint expected_next_start_month;
      gint expected_next_start_day;
      gint expected_next_start_hour;
      gint expected_next_start_minute;
      gdouble expected_next_start_seconds;
      const gchar *expected_next_start_tz;

      gint expected_next_end_year;
      gint expected_next_end_month;
      gint expected_next_end_day;
      gint expected_next_end_hour;
      gint expected_next_end_minute;
      gdouble expected_next_end_seconds;
      const gchar *expected_next_end_tz;
    }
  vectors[] =
    {
      /* Test boundaries on a simple period-1 weekly repeat. */
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2017, 12, 31, 23, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 1, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 1, 2, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 1, 3, 59, 59.99, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 1, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 7, 23, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 8, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 15, 0, 0, 0.0, "Z", 2018, 1, 15, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 8, 2, 0, 0.0, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 15, 0, 0, 0.0, "Z", 2018, 1, 15, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 8, 3, 59, 59.99, "Z",
        TRUE, 2018, 1, 8, 0, 0, 0.0, "Z", 2018, 1, 8, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 15, 0, 0, 0.0, "Z", 2018, 1, 15, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 1, 8, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 15, 0, 0, 0.0, "Z", 2018, 1, 15, 4, 0, 0.0, "Z" },
      /* The same, but with a period-3 repeat. */
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 1, 1, 2, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 22, 0, 0, 0.0, "Z", 2018, 1, 22, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 1, 8, 2, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 22, 0, 0, 0.0, "Z", 2018, 1, 22, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 1, 15, 2, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 22, 0, 0, 0.0, "Z", 2018, 1, 22, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 1, 22, 2, 0, 0.0, "Z",
        TRUE, 2018, 1, 22, 0, 0, 0.0, "Z", 2018, 1, 22, 4, 0, 0.0, "Z",
        TRUE, 2018, 2, 12, 0, 0, 0.0, "Z", 2018, 2, 12, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 1, 29, 2, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 2, 12, 0, 0, 0.0, "Z", 2018, 2, 12, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 2, 5, 2, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 2, 12, 0, 0, 0.0, "Z", 2018, 2, 12, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_WEEK, 3,
        2018, 2, 12, 2, 0, 0.0, "Z",
        TRUE, 2018, 2, 12, 0, 0, 0.0, "Z", 2018, 2, 12, 4, 0, 0.0, "Z",
        TRUE, 2018, 3, 5, 0, 0, 0.0, "Z", 2018, 3, 5, 4, 0, 0.0, "Z" },
      /* Test hourly repeats. */
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2017, 12, 31, 23, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2018, 1, 1, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 1, 30, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2018, 1, 1, 0, 29, 59.99, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 1, 30, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2018, 1, 1, 0, 30, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 1, 30, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2018, 1, 1, 1, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 1, 30, 0.0, "Z",
        TRUE, 2018, 1, 1, 2, 0, 0.0, "Z", 2018, 1, 1, 2, 30, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 0, 30, 0.0, "Z",
        MWT_PERIOD_REPEAT_HOUR, 1,
        2018, 1, 1, 2, 5, 0.0, "Z",
        TRUE, 2018, 1, 1, 2, 0, 0.0, "Z", 2018, 1, 1, 2, 30, 0.0, "Z",
        TRUE, 2018, 1, 1, 3, 0, 0.0, "Z", 2018, 1, 1, 3, 30, 0.0, "Z" },
      /* Test daily repeats. */
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2017, 12, 31, 23, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2018, 1, 1, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 3, 0, 0, 0.0, "Z", 2018, 1, 3, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2018, 1, 1, 3, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 3, 0, 0, 0.0, "Z", 2018, 1, 3, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2018, 1, 1, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 3, 0, 0, 0.0, "Z", 2018, 1, 3, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2018, 1, 2, 0, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 3, 0, 0, 0.0, "Z", 2018, 1, 3, 4, 0, 0.0, "Z" },
      { 2018, 1, 1, 0, 0, 0.0, "Z", 2018, 1, 1, 4, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 2,
        2018, 1, 3, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 3, 0, 0, 0.0, "Z", 2018, 1, 3, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 5, 0, 0, 0.0, "Z", 2018, 1, 5, 4, 0, 0.0, "Z" },
      /* Test monthly repeats (at period-2). */
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 1, 0, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 1, 1, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 1, 5, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 8, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 1, 31, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 2, 1, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 3, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 3, 1, 1, 0, 0.0, "Z", 2018, 3, 1, 5, 0, 0.0, "Z",
        TRUE, 2018, 5, 1, 1, 0, 0.0, "Z", 2018, 5, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 4, 1, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 5, 1, 1, 0, 0.0, "Z", 2018, 5, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 5, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 5, 1, 1, 0, 0.0, "Z", 2018, 5, 1, 5, 0, 0.0, "Z",
        TRUE, 2018, 7, 1, 1, 0, 0.0, "Z", 2018, 7, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2018, 11, 1, 4, 0, 0.0, "Z",
        TRUE, 2018, 11, 1, 1, 0, 0.0, "Z", 2018, 11, 1, 5, 0, 0.0, "Z",
        TRUE, 2019, 1, 1, 1, 0, 0.0, "Z", 2019, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 2,
        2118, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2118, 1, 1, 1, 0, 0.0, "Z", 2118, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2118, 3, 1, 1, 0, 0.0, "Z", 2118, 3, 1, 5, 0, 0.0, "Z" },
      /* Test yearly repeats. */
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        2018, 1, 1, 0, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        2018, 1, 1, 1, 0, 0.0, "Z",
        TRUE, 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2019, 1, 1, 1, 0, 0.0, "Z", 2019, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        2019, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2019, 1, 1, 1, 0, 0.0, "Z", 2019, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2020, 1, 1, 1, 0, 0.0, "Z", 2020, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        2020, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 2020, 1, 1, 1, 0, 0.0, "Z", 2020, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 2021, 1, 1, 1, 0, 0.0, "Z", 2021, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        3000, 1, 1, 0, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 3000, 1, 1, 1, 0, 0.0, "Z", 3000, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        3000, 1, 1, 1, 0, 0.0, "Z",
        TRUE, 3000, 1, 1, 1, 0, 0.0, "Z", 3000, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 3001, 1, 1, 1, 0, 0.0, "Z", 3001, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        3000, 1, 1, 4, 0, 0.0, "Z",
        TRUE, 3000, 1, 1, 1, 0, 0.0, "Z", 3000, 1, 1, 5, 0, 0.0, "Z",
        TRUE, 3001, 1, 1, 1, 0, 0.0, "Z", 3001, 1, 1, 5, 0, 0.0, "Z" },
      { 2018, 1, 1, 1, 0, 0.0, "Z", 2018, 1, 1, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_YEAR, 1,
        3000, 1, 1, 5, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 3001, 1, 1, 1, 0, 0.0, "Z", 3001, 1, 1, 5, 0, 0.0, "Z" },
      /* Leap year handling. 2020 is a leap year. A period on the 30th of January
       * which repeats monthly should repeat on the last day of February. */
      { 2018, 1, 30, 1, 0, 0.0, "Z", 2018, 1, 30, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 1,
        2018, 1, 30, 4, 0, 0.0, "Z",
        TRUE, 2018, 1, 30, 1, 0, 0.0, "Z", 2018, 1, 30, 5, 0, 0.0, "Z",
        TRUE, 2018, 2, 28, 1, 0, 0.0, "Z", 2018, 2, 28, 5, 0, 0.0, "Z" },
      { 2018, 1, 30, 1, 0, 0.0, "Z", 2018, 1, 30, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 1,
        2018, 2, 28, 4, 0, 0.0, "Z",
        TRUE, 2018, 2, 28, 1, 0, 0.0, "Z", 2018, 2, 28, 5, 0, 0.0, "Z",
        TRUE, 2018, 3, 30, 1, 0, 0.0, "Z", 2018, 3, 30, 5, 0, 0.0, "Z" },
      { 2018, 1, 30, 1, 0, 0.0, "Z", 2018, 1, 30, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 1,
        2020, 1, 28, 4, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2020, 1, 30, 1, 0, 0.0, "Z", 2020, 1, 30, 5, 0, 0.0, "Z" },
      { 2018, 1, 30, 1, 0, 0.0, "Z", 2018, 1, 30, 5, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_MONTH, 1,
        2020, 2, 29, 4, 0, 0.0, "Z",
        TRUE, 2020, 2, 29, 1, 0, 0.0, "Z", 2020, 2, 29, 5, 0, 0.0, "Z",
        TRUE, 2020, 3, 30, 1, 0, 0.0, "Z", 2020, 3, 30, 5, 0, 0.0, "Z" },
      /* Check DST handling. There’s a DST switch on 2018-03-25 in Europe/London,
       * where the clocks go forward 1h at 01:00, so the period 01:00–01:59 does
       * not exist: so we expect no recurrences on 2018-03-25 at all, but expect
       * recurrences during the normal time period (01:30–01:45) in subsequent
       * weeks. */
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 3, 18, 1, 35, 0.0, "Europe/London",
        TRUE, 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 3, 25, 1, 0, 0.0, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 3, 25, 0, 59, 59.99, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 3, 25, 1, 35, 0.0, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 3, 25, 2, 0, 0.0, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 4, 1, 1, 29, 59.99, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 4, 1, 1, 35, 0.0, "Europe/London",
        TRUE, 2018, 4, 1, 1, 30, 0.0, "Europe/London", 2018, 4, 1, 1, 45, 0.0, "Europe/London",
        TRUE, 2018, 4, 8, 1, 30, 0.0, "Europe/London", 2018, 4, 8, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 4, 1, 1, 45, 0.0, "Europe/London",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Europe/London", 0, 0, 0, 0, 0, 0.0, "Europe/London",
        TRUE, 2018, 4, 8, 1, 30, 0.0, "Europe/London", 2018, 4, 8, 1, 45, 0.0, "Europe/London" },
      { 2018, 3, 18, 1, 30, 0.0, "Europe/London", 2018, 3, 18, 1, 45, 0.0, "Europe/London",
        MWT_PERIOD_REPEAT_WEEK, 1,
        2018, 4, 8, 1, 35, 0.0, "Europe/London",
        TRUE, 2018, 4, 8, 1, 30, 0.0, "Europe/London", 2018, 4, 8, 1, 45, 0.0, "Europe/London",
        TRUE, 2018, 4, 15, 1, 30, 0.0, "Europe/London", 2018, 4, 15, 1, 45, 0.0, "Europe/London" },
      /* Test what gnome-control-center does, which is to have two periods: one
       * covers all time (no need to test that, as it doesn’t recur), and the
       * other covers 22:00–06:00 at the start of the Unix time period in 1970,
       * and recurs every day. Test that works, and doesn’t perform badly. (This
       * is an implicit performance test, in that the test shouldn’t take
       * forever to run, rather than an explicit one counting loop cycles. We’re
       * not *that* concerned about performance. */
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        2018, 2, 1, 21, 59, 59.99, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 2, 1, 22, 0, 0.0, "Z", 2018, 2, 2, 6, 0, 0.0, "Z" },
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        2018, 2, 1, 22, 0, 0.0, "Z",
        TRUE, 2018, 2, 1, 22, 0, 0.0, "Z", 2018, 2, 2, 6, 0, 0.0, "Z",
        TRUE, 2018, 2, 2, 22, 0, 0.0, "Z", 2018, 2, 3, 6, 0, 0.0, "Z" },
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        2018, 2, 2, 1, 0, 0.0, "Z",
        TRUE, 2018, 2, 1, 22, 0, 0.0, "Z", 2018, 2, 2, 6, 0, 0.0, "Z",
        TRUE, 2018, 2, 2, 22, 0, 0.0, "Z", 2018, 2, 3, 6, 0, 0.0, "Z" },
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        2018, 2, 2, 6, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        TRUE, 2018, 2, 2, 22, 0, 0.0, "Z", 2018, 2, 3, 6, 0, 0.0, "Z" },
      /* Test situations where there is no next recurrence (THE END OF TIME),
       * where either the next recurring start time, or end time, or both, would
       * overflow #GDateTime. */
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 29, 22, 0, 0.0, "Z",
        TRUE, 9999, 12, 29, 22, 0, 0.0, "Z", 9999, 12, 30, 6, 0, 0.0, "Z",
        TRUE, 9999, 12, 30, 22, 0, 0.0, "Z", 9999, 12, 31, 6, 0, 0.0, "Z" },
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 30, 22, 0, 0.0, "Z",
        TRUE, 9999, 12, 30, 22, 0, 0.0, "Z", 9999, 12, 31, 6, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z" },
      { 1970, 1, 1, 22, 0, 0.0, "Z", 1970, 1, 2, 6, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 31, 22, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z" },
      { 2018, 1, 1, 22, 0, 0.0, "Z", 2018, 1, 1, 23, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 29, 22, 0, 0.0, "Z",
        TRUE, 9999, 12, 29, 22, 0, 0.0, "Z", 9999, 12, 29, 23, 0, 0.0, "Z",
        TRUE, 9999, 12, 30, 22, 0, 0.0, "Z", 9999, 12, 30, 23, 0, 0.0, "Z" },
      { 2018, 1, 1, 22, 0, 0.0, "Z", 2018, 1, 1, 23, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 30, 22, 0, 0.0, "Z",
        TRUE, 9999, 12, 30, 22, 0, 0.0, "Z", 9999, 12, 30, 23, 0, 0.0, "Z",
        TRUE, 9999, 12, 31, 22, 0, 0.0, "Z", 9999, 12, 31, 23, 0, 0.0, "Z" },
      { 2018, 1, 1, 22, 0, 0.0, "Z", 2018, 1, 1, 23, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 31, 22, 0, 0.0, "Z",
        TRUE, 9999, 12, 31, 22, 0, 0.0, "Z", 9999, 12, 31, 23, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z" },
      { 2018, 1, 1, 22, 0, 0.0, "Z", 2018, 1, 1, 23, 0, 0.0, "Z",
        MWT_PERIOD_REPEAT_DAY, 1,
        9999, 12, 31, 23, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z",
        FALSE, 0, 0, 0, 0, 0, 0.0, "Z", 0, 0, 0, 0, 0, 0.0, "Z" },
      /* TODO: add more tests where next_period doesn’t exist due to DST change */
      /* TODO: what about tests where a recurrence partially overlaps a DST hole? or is a superset of one? */
      /* TODO: Can we have something which is 2 days long which recurs every day? */
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GTimeZone) start_tz = g_time_zone_new (vectors[i].start_tz);
      g_autoptr(GDateTime) start =
          g_date_time_new (start_tz, vectors[i].start_year, vectors[i].start_month,
                           vectors[i].start_day, vectors[i].start_hour,
                           vectors[i].start_minute, vectors[i].start_seconds);
      g_autoptr(GTimeZone) end_tz = g_time_zone_new (vectors[i].end_tz);
      g_autoptr(GDateTime) end =
          g_date_time_new (end_tz, vectors[i].end_year, vectors[i].end_month,
                           vectors[i].end_day, vectors[i].end_hour,
                           vectors[i].end_minute, vectors[i].end_seconds);
      g_autoptr(GTimeZone) when_tz = g_time_zone_new (vectors[i].when_tz);
      g_autoptr(GDateTime) when =
          g_date_time_new (when_tz, vectors[i].when_year, vectors[i].when_month,
                           vectors[i].when_day, vectors[i].when_hour,
                           vectors[i].when_minute, vectors[i].when_seconds);

      g_autoptr(GDateTime) expected_contains_start = NULL;
      g_autoptr(GDateTime) expected_contains_end = NULL;

      if (vectors[i].expected_contains)
        {
          g_autoptr(GTimeZone) expected_contains_start_tz = g_time_zone_new (vectors[i].expected_contains_start_tz);
          expected_contains_start =
              g_date_time_new (expected_contains_start_tz, vectors[i].expected_contains_start_year, vectors[i].expected_contains_start_month,
                               vectors[i].expected_contains_start_day, vectors[i].expected_contains_start_hour,
                               vectors[i].expected_contains_start_minute, vectors[i].expected_contains_start_seconds);
          g_autoptr(GTimeZone) expected_contains_end_tz = g_time_zone_new (vectors[i].expected_contains_end_tz);
          expected_contains_end =
              g_date_time_new (expected_contains_end_tz, vectors[i].expected_contains_end_year, vectors[i].expected_contains_end_month,
                               vectors[i].expected_contains_end_day, vectors[i].expected_contains_end_hour,
                               vectors[i].expected_contains_end_minute, vectors[i].expected_contains_end_seconds);
        }

      g_autoptr(GDateTime) expected_next_start = NULL;
      g_autoptr(GDateTime) expected_next_end = NULL;

      if (vectors[i].expected_next)
        {
          g_autoptr(GTimeZone) expected_next_start_tz = g_time_zone_new (vectors[i].expected_next_start_tz);
          expected_next_start =
              g_date_time_new (expected_next_start_tz, vectors[i].expected_next_start_year, vectors[i].expected_next_start_month,
                               vectors[i].expected_next_start_day, vectors[i].expected_next_start_hour,
                               vectors[i].expected_next_start_minute, vectors[i].expected_next_start_seconds);
          g_autoptr(GTimeZone) expected_next_end_tz = g_time_zone_new (vectors[i].expected_next_end_tz);
          expected_next_end =
              g_date_time_new (expected_next_end_tz, vectors[i].expected_next_end_year, vectors[i].expected_next_end_month,
                               vectors[i].expected_next_end_day, vectors[i].expected_next_end_hour,
                               vectors[i].expected_next_end_minute, vectors[i].expected_next_end_seconds);
        }

      g_autofree gchar *start_str = g_date_time_format (start, "%FT%T%:::z");
      g_autofree gchar *end_str = g_date_time_format (end, "%FT%T%:::z");
      g_autofree gchar *when_str = g_date_time_format (when, "%FT%T%:::z");

      g_test_message ("Vector %" G_GSIZE_FORMAT ": start %s, end %s, when %s",
                      i, start_str, end_str, when_str);

      g_autoptr(MwtPeriod) period =
          mwt_period_new (start, end, vectors[i].repeat_type,
                          vectors[i].repeat_period,
                          /* leave everything as default */
                          NULL);

      g_autoptr(GDateTime) out_contains_start = NULL;
      g_autoptr(GDateTime) out_contains_end = NULL;
      g_autoptr(GDateTime) out_next_start = NULL;
      g_autoptr(GDateTime) out_next_end = NULL;

      gboolean actual_contains =
          mwt_period_contains_time (period, when, &out_contains_start, &out_contains_end);
      gboolean actual_next =
          mwt_period_get_next_recurrence (period, when, &out_next_start, &out_next_end);

      if (vectors[i].expected_contains)
        {
          g_assert_true (actual_contains);
          g_assert_nonnull (out_contains_start);
          g_assert_nonnull (out_contains_end);
          g_assert_true (g_date_time_equal (out_contains_start, expected_contains_start));
          g_assert_true (g_date_time_equal (out_contains_end, expected_contains_end));
        }
      else
        {
          g_assert_false (actual_contains);
          g_assert_null (out_contains_start);
          g_assert_null (out_contains_end);
        }

      if (vectors[i].expected_next)
        {
          g_assert_true (actual_next);
          g_assert_nonnull (out_next_start);
          g_assert_nonnull (out_next_end);
          g_assert_true (g_date_time_equal (out_next_start, expected_next_start));
          g_assert_true (g_date_time_equal (out_next_end, expected_next_end));
        }
      else
        {
          g_assert_false (actual_next);
          g_assert_null (out_next_start);
          g_assert_null (out_next_end);
        }
    }
}

/* Test overflow handling in mwt_period_contains_time(). */
static void
test_period_contains_time_overflow (void)
{
  /* This tries to trigger an overflow in the overflow check at the top of
   * date_time_add_repeat_period(). */
  g_autoptr(GDateTime) start1 = g_date_time_new_utc (2018, 2, 1, 1, 0, 0.0);
  g_autoptr(GDateTime) end1 = g_date_time_new_utc (2018, 2, 1, 5, 0, 0.0);
  g_autoptr(GDateTime) when1 = g_date_time_new_utc (9999, 1, 1, 2, 0, 0.0);

  g_autoptr(MwtPeriod) period1 =
      mwt_period_new (start1, end1, MWT_PERIOD_REPEAT_YEAR, G_MAXUINT,
                      /* leave everything as default */
                      NULL);
  g_autoptr(GDateTime) out_start1 = NULL;
  g_autoptr(GDateTime) out_end1 = NULL;
  g_assert_false (mwt_period_contains_time (period1, when1, &out_start1, &out_end1));
  g_assert_null (out_start1);
  g_assert_null (out_end1);

  /* This tries to trigger an overflow in the g_date_time_add_days() call in
   * date_time_add_repeat_period(). However, I think min_n_periods is always
   * calculated such that an overflow is not possible. Keep the test case just
   * in case. */
  g_autoptr(GDateTime) start2 = g_date_time_new_utc (9999, 12, 31, 1, 0, 0.0);
  g_autoptr(GDateTime) end2 = g_date_time_new_utc (9999, 12, 31, 5, 0, 0.0);
  g_autoptr(GDateTime) when2 = g_date_time_new_utc (9999, 12, 31, 6, 0, 0.0);

  g_autoptr(MwtPeriod) period2 =
      mwt_period_new (start2, end2, MWT_PERIOD_REPEAT_DAY, 1,
                      /* leave everything as default */
                      NULL);
  g_autoptr(GDateTime) out_start2 = NULL;
  g_autoptr(GDateTime) out_end2 = NULL;
  g_assert_false (mwt_period_contains_time (period2, when2, &out_start2, &out_end2));
  g_assert_null (out_start2);
  g_assert_null (out_end2);
}

/* Test that calling mwt_period_next_recurrence() with a %NULL #GDateTime gives
 * the base time for the #MwtPeriod, regardless of whether the period has any
 * recurrences. */
static void
test_period_next_recurrence_first (void)
{
  g_autoptr(GDateTime) start = g_date_time_new_utc (2018, 2, 1, 1, 0, 0.0);
  g_autoptr(GDateTime) end = g_date_time_new_utc (2018, 2, 1, 5, 0, 0.0);

  g_autoptr(MwtPeriod) period1 =
      mwt_period_new (start, end, MWT_PERIOD_REPEAT_NONE, 0,
                      /* leave everything as default */
                      NULL);
  g_autoptr(GDateTime) out_start1 = NULL;
  g_autoptr(GDateTime) out_end1 = NULL;
  g_assert_true (mwt_period_get_next_recurrence (period1, NULL, &out_start1, &out_end1));
  g_assert_true (g_date_time_equal (start, out_start1));
  g_assert_true (g_date_time_equal (end, out_end1));

  g_autoptr(MwtPeriod) period2 =
      mwt_period_new (start, end, MWT_PERIOD_REPEAT_DAY, 1,
                      /* leave everything as default */
                      NULL);
  g_autoptr(GDateTime) out_start2 = NULL;
  g_autoptr(GDateTime) out_end2 = NULL;
  g_assert_true (mwt_period_get_next_recurrence (period2, NULL, &out_start2, &out_end2));
  g_assert_true (g_date_time_equal (start, out_start2));
  g_assert_true (g_date_time_equal (end, out_end2));
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
  g_test_add_func ("/period/contains-time", test_period_contains_time);
  g_test_add_func ("/period/contains-time/overflow", test_period_contains_time_overflow);
  g_test_add_func ("/period/next-recurrence/first", test_period_next_recurrence_first);

  return g_test_run ();
}
