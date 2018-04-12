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
#include <libmogwai-tariff/tariff-builder.h>
#include <locale.h>

#include "common.h"

/* Test resetting an empty tariff builder doesn’t crash. */
static void
test_tariff_builder_reset_empty (void)
{
  g_autoptr(MwtTariffBuilder) builder = NULL;

  builder = mwt_tariff_builder_new ();

  /* Do it a couple of times for good measure. */
  for (guint i = 0; i < 2; i++)
    {
      mwt_tariff_builder_reset (builder);
      g_assert_null (mwt_tariff_builder_get_tariff (builder));
      g_assert_null (mwt_tariff_builder_get_tariff_as_variant (builder));
      g_assert_null (mwt_tariff_builder_get_tariff_as_bytes (builder));
    }
}

/* Test resetting a partially-filled tariff builder doesn’t crash. */
static void
test_tariff_builder_reset_partial (void)
{
  g_autoptr(MwtTariffBuilder) builder = NULL;

  builder = mwt_tariff_builder_new ();

  mwt_tariff_builder_set_name (builder, "test-tariff");

  g_autoptr(GDateTime) start1 = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) end1 = g_date_time_new_utc (2018, 2, 1, 0, 0, 0);

  g_autoptr(MwtPeriod) period = NULL;
  period = mwt_period_new (start1, end1, MWT_PERIOD_REPEAT_NONE, 0, NULL);
  mwt_tariff_builder_add_period (builder, period);

  mwt_tariff_builder_reset (builder);
  g_assert_null (mwt_tariff_builder_get_tariff (builder));
  g_assert_null (mwt_tariff_builder_get_tariff_as_variant (builder));
  g_assert_null (mwt_tariff_builder_get_tariff_as_bytes (builder));
}

/* Test building a simple tariff. This tariff:
 *  • Starts on 2018-01-01 and runs forever.
 *  • Has period1 which limits monthly capacity to 2GB.
 *  • Has period2 which uncaps the capacity each weekend.
 */
static void
test_tariff_builder_simple (void)
{
  g_autoptr(MwtTariffBuilder) builder = NULL;

  /* Build the tariff. */
  builder = mwt_tariff_builder_new ();
  mwt_tariff_builder_set_name (builder, "test-tariff");

  /* Period 1. */
  g_autoptr(GDateTime) start1 = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) end1 = g_date_time_new_utc (2018, 2, 1, 0, 0, 0);

  g_autoptr(MwtPeriod) period1 = NULL;
  period1 = mwt_period_new (start1, end1, MWT_PERIOD_REPEAT_MONTH, 1,
                            "capacity-limit", G_GUINT64_CONSTANT (2 * 1000 * 1000 * 1000),
                            NULL);
  mwt_tariff_builder_add_period (builder, period1);

  /* Period 2. */
  g_autoptr(GDateTime) start2 = g_date_time_new_utc (2018, 1, 6, 0, 0, 0);
  g_autoptr(GDateTime) end2 = g_date_time_new_utc (2018, 1, 8, 0, 0, 0);

  g_autoptr(MwtPeriod) period2 = NULL;
  period2 = mwt_period_new (start2, end2, MWT_PERIOD_REPEAT_WEEK, 1,
                            "capacity-limit", G_MAXUINT64,
                            NULL);
  mwt_tariff_builder_add_period (builder, period2);

  /* Finish building the tariff and check its properties. */
  g_autoptr(MwtTariff) tariff = NULL;
  tariff = mwt_tariff_builder_get_tariff (builder);
  g_assert_nonnull (tariff);

  g_assert_cmpstr (mwt_tariff_get_name (tariff), ==, "test-tariff");
  GPtrArray *periods = mwt_tariff_get_periods (tariff);
  g_assert_nonnull (periods);
  g_assert_cmpuint (periods->len, ==, 2);

  MwtPeriod *period1_built = g_ptr_array_index (periods, 0);
  assert_periods_equal (period1_built, period1);
  MwtPeriod *period2_built = g_ptr_array_index (periods, 1);
  assert_periods_equal (period2_built, period2);

  g_autoptr(GVariant) variant = NULL;
  variant = mwt_tariff_builder_get_tariff_as_variant (builder);
  g_assert_nonnull (variant);
  g_assert_false (g_variant_is_floating (variant));
  g_assert_true (g_variant_is_of_type (variant, G_VARIANT_TYPE ("(sqv)")));

  g_autoptr(GBytes) bytes = NULL;
  bytes = mwt_tariff_builder_get_tariff_as_bytes (builder);
  g_assert_nonnull (bytes);
  g_assert_cmpuint (g_bytes_get_size (bytes), ==, 140);
}

/* Test building a tariff with no periods. This should fail. */
static void
test_tariff_builder_empty (void)
{
  g_autoptr(MwtTariffBuilder) builder = NULL;

  /* Build the tariff. */
  builder = mwt_tariff_builder_new ();
  mwt_tariff_builder_set_name (builder, "test-tariff");

  /* Finish building the tariff and check its properties. */
  g_autoptr(MwtTariff) tariff = NULL;
  tariff = mwt_tariff_builder_get_tariff (builder);
  g_assert_null (tariff);
  g_assert_null (mwt_tariff_builder_get_tariff_as_variant (builder));
  g_assert_null (mwt_tariff_builder_get_tariff_as_bytes (builder));
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/tariff-builder/reset/empty", test_tariff_builder_reset_empty);
  g_test_add_func ("/tariff-builder/reset/partial", test_tariff_builder_reset_partial);
  g_test_add_func ("/tariff-builder/simple", test_tariff_builder_simple);
  g_test_add_func ("/tariff-builder/empty", test_tariff_builder_empty);

  return g_test_run ();
}
