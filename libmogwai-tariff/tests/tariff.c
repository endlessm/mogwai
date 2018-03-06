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
#include <libmogwai-tariff/tariff.h>
#include <locale.h>


/* Test the GObject properties on a tariff. */
static void
test_tariff_properties (void)
{
  g_autoptr(MwtTariff) tariff = NULL;
  g_autoptr(GPtrArray) periods = g_ptr_array_new_with_free_func (NULL);
  g_autoptr(GDateTime) period_start = g_date_time_new_utc (2018, 1, 22, 0, 0, 0);
  g_autoptr(GDateTime) period_end = g_date_time_new_utc (2018, 2, 22, 0, 0, 0);
  g_autoptr(MwtPeriod) period = mwt_period_new (period_start, period_end,
                                                MWT_PERIOD_REPEAT_NONE, 0,
                                                NULL);
  g_ptr_array_add (periods, period);
  tariff = mwt_tariff_new ("name", periods);
  g_assert_true (MWT_IS_TARIFF (tariff));

  g_assert_cmpstr (mwt_tariff_get_name (tariff), ==, "name");
  GPtrArray *actual_periods = mwt_tariff_get_periods (tariff);
  g_assert_cmpuint (actual_periods->len, ==, 1);

  g_autofree gchar *actual_name = NULL;
  g_autoptr(GPtrArray) actual_periods2 = NULL;

  g_object_get (G_OBJECT (tariff),
                "name", &actual_name,
                "periods", &actual_periods2,
                NULL);

  g_assert_cmpstr (actual_name, ==, "name");
  g_assert_cmpuint (actual_periods2->len, ==, 1);
}

/* Test mwt_tariff_lookup_period() with various dates/times for a generic
 * tariff. */
static void
test_tariff_lookup (void)
{
  /* Construct a tariff. */
  g_autoptr(GPtrArray) periods = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period1_start = g_date_time_new_utc (2018, 1, 22, 0, 0, 0);
  g_autoptr(GDateTime) period1_end = g_date_time_new_utc (2018, 2, 22, 0, 0, 0);
  g_autoptr(MwtPeriod) period1 = mwt_period_new (period1_start, period1_end,
                                                 MWT_PERIOD_REPEAT_NONE, 0,
                                                 "capacity-limit", (guint64) 2000,
                                                 NULL);
  g_ptr_array_add (periods, period1);

  g_autoptr(GDateTime) period2_start = g_date_time_new_utc (2018, 1, 22, 0, 0, 0);
  g_autoptr(GDateTime) period2_end = g_date_time_new_utc (2018, 1, 22, 12, 0, 0);
  g_autoptr(MwtPeriod) period2 = mwt_period_new (period2_start, period2_end,
                                                 MWT_PERIOD_REPEAT_WEEK, 1,
                                                 "capacity-limit", (guint64) 1000,
                                                 NULL);
  g_ptr_array_add (periods, period2);

  g_autoptr(MwtTariff) tariff = NULL;
  tariff = mwt_tariff_new ("name", periods);

  /* Check whether periods are looked up correctly. */
  const struct
    {
      gint year;
      gint month;
      gint day;
      gint hour;
      gint minute;
      gdouble seconds;
      MwtPeriod *expected_period;
    }
  vectors[] =
    {
      { 2018, 1, 21, 23, 59, 59, NULL },
      { 2018, 1, 22, 0, 0, 0, period2 },
      { 2018, 1, 22, 1, 0, 0, period2 },
      { 2018, 1, 22, 11, 59, 59, period2 },
      { 2018, 1, 22, 12, 0, 0, period1 },
      { 2018, 1, 22, 12, 0, 1, period1 },
      { 2018, 1, 23, 0, 0, 0, period1 },
      { 2018, 2, 21, 23, 59, 59, period1 },
      { 2018, 2, 22, 0, 0, 0, NULL },
      /* FIXME: Test recurrence */
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_test_message ("Vector %" G_GSIZE_FORMAT ": %04d-%02d-%02dT%02d:%02d:%02fZ, %p",
                      i, vectors[i].year, vectors[i].month, vectors[i].day,
                      vectors[i].hour, vectors[i].minute, vectors[i].seconds,
                      vectors[i].expected_period);
      g_autoptr(GDateTime) lookup_date =
          g_date_time_new_utc (vectors[i].year, vectors[i].month, vectors[i].day,
                               vectors[i].hour, vectors[i].minute, vectors[i].seconds);
      MwtPeriod *lookup_period = mwt_tariff_lookup_period (tariff, lookup_date);
      if (vectors[i].expected_period == NULL)
        {
          g_assert_null (lookup_period);
        }
      else
        {
          g_assert_true (MWT_IS_PERIOD (lookup_period));
          g_assert_true (lookup_period == vectors[i].expected_period);
        }
    }
}

/* Test getting the next transition using mwt_tariff_get_next_transition(), and
 * check that it’s calculated correctly, and returns the correct to/from periods
 * for a variety of tariffs and date/times. */
static void
test_tariff_next_transition (void)
{
  /* Construct various test tariffs. Firstly, one with a non-recurring period. */
  g_autoptr(GPtrArray) periods1 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period1a_start = g_date_time_new_utc (2018, 1, 22, 0, 0, 0);
  g_autoptr(GDateTime) period1a_end = g_date_time_new_utc (2018, 2, 22, 0, 0, 0);
  g_autoptr(MwtPeriod) period1a = mwt_period_new (period1a_start, period1a_end,
                                                  MWT_PERIOD_REPEAT_NONE, 0,
                                                  NULL);
  g_ptr_array_add (periods1, period1a);

  g_autoptr(MwtTariff) tariff1 = mwt_tariff_new ("name", periods1);

  /* One with a single recurring period. */
  g_autoptr(GPtrArray) periods2 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period2a_start = g_date_time_new_utc (2018, 1, 10, 2, 0, 0);
  g_autoptr(GDateTime) period2a_end = g_date_time_new_utc (2018, 1, 10, 6, 0, 0);
  g_autoptr(MwtPeriod) period2a = mwt_period_new (period2a_start, period2a_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods2, period2a);

  g_autoptr(MwtTariff) tariff2 = mwt_tariff_new ("name", periods2);

  /* One period which covers all time, and another recurring on top. */
  g_autoptr(GPtrArray) periods3 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period3a_start = g_date_time_new_utc (1970, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) period3a_end = g_date_time_new_utc (9999, 12, 31, 23, 59, 59.99);
  g_autoptr(MwtPeriod) period3a = mwt_period_new (period3a_start, period3a_end,
                                                  MWT_PERIOD_REPEAT_NONE, 0,
                                                  NULL);
  g_ptr_array_add (periods3, period3a);

  g_autoptr(GDateTime) period3b_start = g_date_time_new_utc (2018, 1, 10, 2, 0, 0);
  g_autoptr(GDateTime) period3b_end = g_date_time_new_utc (2018, 1, 10, 6, 0, 0);
  g_autoptr(MwtPeriod) period3b = mwt_period_new (period3b_start, period3b_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods3, period3b);

  g_autoptr(MwtTariff) tariff3 = mwt_tariff_new ("name", periods3);

  /* Two recurring periods, one inside the other. */
  g_autoptr(GPtrArray) periods4 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period4a_start = g_date_time_new_utc (2018, 1, 8, 1, 0, 0);
  g_autoptr(GDateTime) period4a_end = g_date_time_new_utc (2018, 1, 14, 22, 0, 0);
  g_autoptr(MwtPeriod) period4a = mwt_period_new (period4a_start, period4a_end,
                                                  MWT_PERIOD_REPEAT_WEEK, 1,
                                                  NULL);
  g_ptr_array_add (periods4, period4a);

  g_autoptr(GDateTime) period4b_start = g_date_time_new_utc (2018, 1, 8, 2, 0, 0);
  g_autoptr(GDateTime) period4b_end = g_date_time_new_utc (2018, 1, 8, 6, 0, 0);
  g_autoptr(MwtPeriod) period4b = mwt_period_new (period4b_start, period4b_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods4, period4b);

  g_autoptr(MwtTariff) tariff4 = mwt_tariff_new ("name", periods4);

  /* Two recurring periods, ending at the same time. */
  g_autoptr(GPtrArray) periods5 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period5a_start = g_date_time_new_utc (2018, 1, 8, 10, 0, 0);
  g_autoptr(GDateTime) period5a_end = g_date_time_new_utc (2018, 1, 8, 22, 0, 0);
  g_autoptr(MwtPeriod) period5a = mwt_period_new (period5a_start, period5a_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods5, period5a);

  g_autoptr(GDateTime) period5b_start = g_date_time_new_utc (2018, 1, 8, 12, 0, 0);
  g_autoptr(GDateTime) period5b_end = g_date_time_new_utc (2018, 1, 8, 22, 0, 0);
  g_autoptr(MwtPeriod) period5b = mwt_period_new (period5b_start, period5b_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods5, period5b);

  g_autoptr(MwtTariff) tariff5 = mwt_tariff_new ("name", periods5);

  /* Two recurring periods, each starting as the other ends. */
  g_autoptr(GPtrArray) periods6 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period6a_start = g_date_time_new_utc (2018, 1, 8, 0, 0, 0);
  g_autoptr(GDateTime) period6a_end = g_date_time_new_utc (2018, 1, 9, 0, 0, 0);
  g_autoptr(MwtPeriod) period6a = mwt_period_new (period6a_start, period6a_end,
                                                  MWT_PERIOD_REPEAT_DAY, 2,
                                                  NULL);
  g_ptr_array_add (periods6, period6a);

  g_autoptr(GDateTime) period6b_start = g_date_time_new_utc (2018, 1, 9, 0, 0, 0);
  g_autoptr(GDateTime) period6b_end = g_date_time_new_utc (2018, 1, 10, 0, 0, 0);
  g_autoptr(MwtPeriod) period6b = mwt_period_new (period6b_start, period6b_end,
                                                  MWT_PERIOD_REPEAT_DAY, 2,
                                                  NULL);
  g_ptr_array_add (periods6, period6b);

  g_autoptr(MwtTariff) tariff6 = mwt_tariff_new ("name", periods6);

  /* Two recurring periods, starting at the same time. */
  g_autoptr(GPtrArray) periods7 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period7a_start = g_date_time_new_utc (2018, 1, 8, 10, 0, 0);
  g_autoptr(GDateTime) period7a_end = g_date_time_new_utc (2018, 1, 8, 22, 0, 0);
  g_autoptr(MwtPeriod) period7a = mwt_period_new (period7a_start, period7a_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods7, period7a);

  g_autoptr(GDateTime) period7b_start = g_date_time_new_utc (2018, 1, 8, 10, 0, 0);
  g_autoptr(GDateTime) period7b_end = g_date_time_new_utc (2018, 1, 8, 12, 0, 0);
  g_autoptr(MwtPeriod) period7b = mwt_period_new (period7b_start, period7b_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods7, period7b);

  g_autoptr(MwtTariff) tariff7 = mwt_tariff_new ("name", periods7);

  /* One period, recurring so it starts when it ends. */
  g_autoptr(GPtrArray) periods8 = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) period8a_start = g_date_time_new_utc (2018, 1, 8, 0, 0, 0);
  g_autoptr(GDateTime) period8a_end = g_date_time_new_utc (2018, 1, 9, 0, 0, 0);
  g_autoptr(MwtPeriod) period8a = mwt_period_new (period8a_start, period8a_end,
                                                  MWT_PERIOD_REPEAT_DAY, 1,
                                                  NULL);
  g_ptr_array_add (periods8, period8a);

  g_autoptr(MwtTariff) tariff8 = mwt_tariff_new ("name", periods8);

  const struct
    {
      MwtTariff *tariff;

      gint after_year;
      gint after_month;
      gint after_day;
      gint after_hour;
      gint after_minute;
      gdouble after_seconds;
      const gchar *after_tz;

      gint expected_next_year;
      gint expected_next_month;
      gint expected_next_day;
      gint expected_next_hour;
      gint expected_next_minute;
      gdouble expected_next_seconds;
      const gchar *expected_next_tz;  /* (nullable) */

      gboolean expected_next_is_first;

      MwtPeriod *expected_from_period;  /* (nullable) */
      MwtPeriod *expected_to_period;  /* (nullable) */
    }
  vectors[] =
    {
      /* Some simple tests with a single period tariff, with no recurrence. */
      { tariff1,
        2018, 1, 1, 0, 0, 0.0, "Z",
        2018, 1, 22, 0, 0, 0.0, "Z", TRUE,
        NULL, period1a },
      { tariff1,
        2018, 1, 21, 23, 59, 59.99, "Z",
        2018, 1, 22, 0, 0, 0.0, "Z", FALSE,
        NULL, period1a },
      { tariff1,
        2018, 1, 22, 0, 0, 0.0, "Z",
        2018, 2, 22, 0, 0, 0.0, "Z", FALSE,
        period1a, NULL },
      { tariff1,
        2018, 2, 10, 0, 0, 0.0, "Z",
        2018, 2, 22, 0, 0, 0.0, "Z", FALSE,
        period1a, NULL },
      { tariff1,
        2018, 2, 21, 23, 59, 59.99, "Z",
        2018, 2, 22, 0, 0, 0.0, "Z", FALSE,
        period1a, NULL },
      { tariff1,
        2018, 2, 22, 0, 0, 0.0, "Z",
        0, 0, 0, 0, 0, 0.0, NULL, FALSE,
        NULL, NULL },
      { tariff1,
        2018, 3, 1, 0, 0, 0.0, "Z",
        0, 0, 0, 0, 0, 0.0, NULL, FALSE,
        NULL, NULL },
      /* A single recurring period. */
      { tariff2,
        2018, 1, 1, 0, 0, 0.0, "Z",
        2018, 1, 10, 2, 0, 0.0, "Z", TRUE,
        NULL, period2a },
      { tariff2,
        2018, 1, 10, 2, 0, 0.0, "Z",
        2018, 1, 10, 6, 0, 0.0, "Z", FALSE,
        period2a, NULL },
      { tariff2,
        2018, 1, 10, 4, 0, 0.0, "Z",
        2018, 1, 10, 6, 0, 0.0, "Z", FALSE,
        period2a, NULL },
      { tariff2,
        2018, 1, 10, 6, 0, 0.0, "Z",
        2018, 1, 11, 2, 0, 0.0, "Z", FALSE,
        NULL, period2a },
      { tariff2,
        2018, 1, 11, 4, 0, 0.0, "Z",
        2018, 1, 11, 6, 0, 0.0, "Z", FALSE,
        period2a, NULL },
      /* A recurring period over one which lasts for all time (the g-c-c case). */
      { tariff3,
        1990, 1, 1, 0, 0, 0.0, "Z",
        2018, 1, 10, 2, 0, 0.0, "Z", FALSE,
        period3a, period3b },
      { tariff3,
        2018, 1, 10, 1, 59, 59.99, "Z",
        2018, 1, 10, 2, 0, 0.0, "Z", FALSE,
        period3a, period3b },
      { tariff3,
        2018, 1, 10, 2, 0, 0.0, "Z",
        2018, 1, 10, 6, 0, 0.0, "Z", FALSE,
        period3b, period3a },
      { tariff3,
        2018, 1, 10, 4, 0, 0.0, "Z",
        2018, 1, 10, 6, 0, 0.0, "Z", FALSE,
        period3b, period3a },
      { tariff3,
        2018, 1, 10, 6, 0, 0.0, "Z",
        2018, 1, 11, 2, 0, 0.0, "Z", FALSE,
        period3a, period3b },
      { tariff3,
        2018, 1, 11, 2, 0, 0.0, "Z",
        2018, 1, 11, 6, 0, 0.0, "Z", FALSE,
        period3b, period3a },
      { tariff3,
        9999, 12, 31, 4, 0, 0.0, "Z",
        9999, 12, 31, 6, 0, 0.0, "Z", FALSE,
        period3b, period3a },
      { tariff3,
        9999, 12, 31, 6, 0, 0.0, "Z",
        9999, 12, 31, 23, 59, 59.99, "Z", FALSE,
        period3a, NULL },
      /* Two recurring periods, one inside the other. */
      { tariff4,
        2018, 1, 1, 1, 0, 0.0, "Z",
        2018, 1, 8, 1, 0, 0.0, "Z", TRUE,
        NULL, period4a },
      { tariff4,
        2018, 1, 8, 1, 0, 0.0, "Z",
        2018, 1, 8, 2, 0, 0.0, "Z", FALSE,
        period4a, period4b },
      { tariff4,
        2018, 1, 8, 2, 0, 0.0, "Z",
        2018, 1, 8, 6, 0, 0.0, "Z", FALSE,
        period4b, period4a },
      { tariff4,
        2018, 1, 8, 6, 0, 0.0, "Z",
        2018, 1, 9, 2, 0, 0.0, "Z", FALSE,
        period4a, period4b },
      { tariff4,
        2018, 1, 14, 1, 0, 0.0, "Z",
        2018, 1, 14, 2, 0, 0.0, "Z", FALSE,
        period4a, period4b },
      { tariff4,
        2018, 1, 14, 2, 0, 0.0, "Z",
        2018, 1, 14, 6, 0, 0.0, "Z", FALSE,
        period4b, period4a },
      { tariff4,
        2018, 1, 14, 6, 0, 0.0, "Z",
        2018, 1, 14, 22, 0, 0.0, "Z", FALSE,
        period4a, NULL },
      { tariff4,
        2018, 1, 14, 22, 0, 0.0, "Z",
        2018, 1, 15, 1, 0, 0.0, "Z", FALSE,
        NULL, period4a },
      { tariff4,
        2018, 1, 15, 1, 0, 0.0, "Z",
        2018, 1, 15, 2, 0, 0.0, "Z", FALSE,
        period4a, period4b },
      /* Two periods, ending at the same time. */
      { tariff5,
        2018, 1, 8, 9, 0, 0.0, "Z",
        2018, 1, 8, 10, 0, 0.0, "Z", TRUE,
        NULL, period5a },
      { tariff5,
        2018, 1, 8, 10, 0, 0.0, "Z",
        2018, 1, 8, 12, 0, 0.0, "Z", FALSE,
        period5a, period5b },
      { tariff5,
        2018, 1, 8, 12, 0, 0.0, "Z",
        2018, 1, 8, 22, 0, 0.0, "Z", FALSE,
        period5b, NULL },
      { tariff5,
        2018, 1, 8, 22, 0, 0.0, "Z",
        2018, 1, 9, 10, 0, 0.0, "Z", FALSE,
        NULL, period5a },
      /* Two periods, each starting as the other ends. */
      { tariff6,
        2018, 1, 7, 10, 0, 0.0, "Z",
        2018, 1, 8, 0, 0, 0.0, "Z", TRUE,
        NULL, period6a },
      { tariff6,
        2018, 1, 8, 0, 0, 0.0, "Z",
        2018, 1, 9, 0, 0, 0.0, "Z", FALSE,
        period6a, period6b },
      { tariff6,
        2018, 1, 9, 0, 0, 0.0, "Z",
        2018, 1, 10, 0, 0, 0.0, "Z", FALSE,
        period6b, period6a },
      { tariff6,
        2018, 1, 10, 0, 0, 0.0, "Z",
        2018, 1, 11, 0, 0, 0.0, "Z", FALSE,
        period6a, period6b },
      { tariff6,
        2018, 1, 11, 0, 0, 0.0, "Z",
        2018, 1, 12, 0, 0, 0.0, "Z", FALSE,
        period6b, period6a },
      /* Two periods, starting at the same time. */
      { tariff7,
        2018, 1, 8, 9, 0, 0.0, "Z",
        2018, 1, 8, 10, 0, 0.0, "Z", TRUE,
        NULL, period7b },
      { tariff7,
        2018, 1, 8, 10, 0, 0.0, "Z",
        2018, 1, 8, 12, 0, 0.0, "Z", FALSE,
        period7b, period7a },
      { tariff7,
        2018, 1, 8, 12, 0, 0.0, "Z",
        2018, 1, 8, 22, 0, 0.0, "Z", FALSE,
        period7a, NULL },
      { tariff7,
        2018, 1, 8, 22, 0, 0.0, "Z",
        2018, 1, 9, 10, 0, 0.0, "Z", FALSE,
        NULL, period7b },
      /* One period, recurring so it starts when it ends. */
      { tariff8,
        2018, 1, 7, 5, 0, 0.0, "Z",
        2018, 1, 8, 0, 0, 0.0, "Z", TRUE,
        NULL, period8a },
      { tariff8,
        2018, 1, 8, 0, 0, 0.0, "Z",
        2018, 1, 9, 0, 0, 0.0, "Z", FALSE,
        period8a, period8a },
      { tariff8,
        2018, 1, 9, 0, 0, 0.0, "Z",
        2018, 1, 10, 0, 0, 0.0, "Z", FALSE,
        period8a, period8a },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GTimeZone) after_tz = g_time_zone_new (vectors[i].after_tz);
      g_autoptr(GDateTime) after =
          g_date_time_new (after_tz, vectors[i].after_year, vectors[i].after_month,
                           vectors[i].after_day, vectors[i].after_hour,
                           vectors[i].after_minute, vectors[i].after_seconds);

      g_autoptr(GDateTime) expected_next = NULL;

      if (vectors[i].expected_next_tz != NULL)
        {
          g_autoptr(GTimeZone) expected_next_tz = g_time_zone_new (vectors[i].expected_next_tz);
          expected_next =
              g_date_time_new (expected_next_tz, vectors[i].expected_next_year, vectors[i].expected_next_month,
                               vectors[i].expected_next_day, vectors[i].expected_next_hour,
                               vectors[i].expected_next_minute, vectors[i].expected_next_seconds);
        }

      g_autofree gchar *after_str = g_date_time_format (after, "%FT%T%:::z");
      g_autofree gchar *expected_next_str =
          (expected_next != NULL) ? g_date_time_format (expected_next, "%FT%T%:::z") : g_strdup ("never");

      g_test_message ("Vector %" G_GSIZE_FORMAT ": after: %s, expected next: %s",
                      i, after_str, expected_next_str);

      g_autoptr(GDateTime) next = NULL;
      MwtPeriod *from_period = NULL, *to_period = NULL;
      next = mwt_tariff_get_next_transition (vectors[i].tariff, after,
                                             &from_period, &to_period);

      if (expected_next != NULL)
        {
          g_assert_nonnull (next);
          g_assert_true (g_date_time_equal (next, expected_next));
          g_assert_true (from_period == vectors[i].expected_from_period);
          g_assert_true (to_period == vectors[i].expected_to_period);
        }
      else
        {
          g_assert_null (next);
          g_assert_null (from_period);
          g_assert_null (to_period);
        }

      /* Test calling with (@after == NULL). */
      if (vectors[i].expected_next_is_first)
        {
          g_autoptr(GDateTime) next2 = NULL;
          MwtPeriod *from_period2 = NULL, *to_period2 = NULL;
          next2 = mwt_tariff_get_next_transition (vectors[i].tariff, NULL,
                                                  &from_period2, &to_period2);

          g_assert_nonnull (next2);
          g_assert_nonnull (expected_next);
          g_assert_true (g_date_time_equal (next2, expected_next));
          g_assert_null (from_period2);
          g_assert_true (to_period2 == vectors[i].expected_to_period);
        }
    }

  /* Special extra test for period 3 to check that (@after == NULL) works to get
   * the first transition. We can’t include this in the test vectors above,
   * because it’s impossible to represent a date/time before the first
   * transition for that tariff. */
  g_autoptr(GDateTime) next = NULL;
  MwtPeriod *from_period = NULL, *to_period = NULL;
  next = mwt_tariff_get_next_transition (tariff3, NULL, &from_period, &to_period);

  g_assert_nonnull (next);
  g_assert_true (g_date_time_equal (next, period3a_start));
  g_assert_null (from_period);
  g_assert_true (to_period == period3a);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/tariff/properties", test_tariff_properties);
  g_test_add_func ("/tariff/lookup", test_tariff_lookup);
  g_test_add_func ("/tariff/next-transition", test_tariff_next_transition);

  return g_test_run ();
}
