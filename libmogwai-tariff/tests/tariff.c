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

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/tariff/properties", test_tariff_properties);
  g_test_add_func ("/tariff/lookup", test_tariff_lookup);

  return g_test_run ();
}
