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
#include <libmogwai-tariff/tariff-loader.h>
#include <locale.h>


/* Test that get_tariff() returns %NULL if nothing’s been loaded. */
static void
test_tariff_loader_unloaded (void)
{
  g_autoptr(MwtTariffLoader) loader = NULL;
  loader = mwt_tariff_loader_new ();

  g_assert_null (mwt_tariff_loader_get_tariff (loader));
}

/* Test the tariff loader handles erroneous files gracefully.
 * Note: These bytes are in version 1 format. */
static void
test_tariff_loader_errors_bytes (void)
{
  const struct
    {
      const guint8 *data;
      gsize length;
    }
  vectors[] =
    {
      /* Empty file */
      { (const guint8 *) "", 0 },
      /* Incorrect magic */
      { (const guint8 *) "\x4c\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\x74\x29\x29\x0e", 46 },
      /* Incorrect magic (not nul terminated) */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x01\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\x74\x29\x29\x0e", 46 },
      /* Incorrect version number */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\xf1\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\x74\x29\x29\x0e", 46 },
      /* Outer variant not in normal form (final byte changed) */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\x74\x29\x29\x00", 46 },
      /* Inner variant type not valid */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\xff\x29\x29\x0e", 46 },
      /* Inner variant invalid (name not nul terminated) */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\xff\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74\x71\x75\x74\x29\x29\x0e", 46 },
      /* Outer variant not valid (truncated) */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x74", 40 },
      /* Inner variant type valid but not correct for V1 of the format */
      { (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                         "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                         "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                         "\x78\x71\x75\x74\x29\x29\x0e", 46 },
      /* FIXME: Test validation of the tariff and period properties. Maybe do
       * that using a GVariant? */
    };

  g_autoptr(MwtTariffLoader) loader = NULL;
  loader = mwt_tariff_loader_new ();

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GBytes) bytes = g_bytes_new_static (vectors[i].data, vectors[i].length);

      gboolean retval = mwt_tariff_loader_load_from_bytes (loader, bytes, &error);
      g_assert_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID);
      g_assert_false (retval);
      g_assert_null (mwt_tariff_loader_get_tariff (loader));
    }
}

/* Test the tariff loader handles erroneous files gracefully, when loading them
 * from a #GVariant. */
static void
test_tariff_loader_errors_variant (void)
{
  const gchar *vectors[] =
    {
      /* Invalid inner variant */
      "('Mogwai tariff', @q 2, <''>)",
      /* Invalid version number */
      "('Mogwai tariff', @q 0, <@(sa(ttssqut)) ('a', [(0, 1, 'UTC', 'UTC', 0, 0, 0)])>)",
      /* Invalid magic */
      "('not magic', @q 2, <@(sa(ttssqut)) ('a', [(0, 1, 'UTC', 'UTC', 0, 0, 0)])>)",
      /* Invalid outer type */
      "('hello there', @q 1)",
      "'boo'",
    };

  g_autoptr(MwtTariffLoader) loader = NULL;
  loader = mwt_tariff_loader_new ();

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GVariant) variant = g_variant_parse (NULL, vectors[i],
                                                     NULL, NULL, &local_error);
      g_assert_no_error (local_error);

      gboolean retval = mwt_tariff_loader_load_from_variant (loader, variant, &local_error);
      g_assert_error (local_error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID);
      g_assert_false (retval);
      g_assert_null (mwt_tariff_loader_get_tariff (loader));
    }
}

/* Test loading a simple tariff from bytes works.
 * Note: These bytes are in version 1 format. */
static void
test_tariff_loader_simple_bytes (void)
{
  const guint8 *data =
    (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66\x00"
                     "\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66\x66\x00"
                     "\x00\x00\x00\x00\x00\x7a\x49\x5a\x00\x00\x00\x00\x80\x58"
                     "\x72\x5a\x00\x00\x00\x00\x04\x00\x00\x00\x01\x00\x00\x00"
                     "\x00\x94\x35\x77\x00\x00\x00\x00\x80\x11\x50\x5a\x00\x00"
                     "\x00\x00\x80\xb4\x52\x5a\x00\x00\x00\x00\x03\x00\x00\x00"
                     "\x01\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x0c\x00"
                     "\x28\x73\x61\x28\x74\x74\x71\x75\x74\x29\x29\x0e";
  const gsize len = 110;

  g_autoptr(GBytes) bytes = g_bytes_new_static (data, len);
  g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();
  g_autoptr(GError) error = NULL;

  gboolean retval = mwt_tariff_loader_load_from_bytes (loader, bytes, &error);
  g_assert_no_error (error);
  g_assert_true (retval);

  MwtTariff *tariff = mwt_tariff_loader_get_tariff (loader);
  g_assert_true (MWT_IS_TARIFF (tariff));

  /* Check properties. */
  g_assert_cmpstr (mwt_tariff_get_name (tariff), ==, "test-tariff");

  GPtrArray *periods = mwt_tariff_get_periods (tariff);
  g_assert_cmpuint (periods->len, ==, 2);

  g_autoptr(GDateTime) period1_start_expected = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) period1_end_expected = g_date_time_new_utc (2018, 2, 1, 0, 0, 0);

  MwtPeriod *period1 = g_ptr_array_index (periods, 0);
  GDateTime *period1_start = mwt_period_get_start (period1);
  GDateTime *period1_end = mwt_period_get_end (period1);
  g_assert_true (g_date_time_equal (period1_start, period1_start_expected));
  g_assert_true (g_date_time_equal (period1_end, period1_end_expected));
  g_assert_cmpint (mwt_period_get_repeat_type (period1), ==, MWT_PERIOD_REPEAT_MONTH);
  g_assert_cmpuint (mwt_period_get_repeat_period (period1), ==, 1);
  g_assert_cmpuint (mwt_period_get_capacity_limit (period1), ==, 2 * 1000 * 1000 * 1000);

  g_autoptr(GDateTime) period2_start_expected = g_date_time_new_utc (2018, 1, 6, 0, 0, 0);
  g_autoptr(GDateTime) period2_end_expected = g_date_time_new_utc (2018, 1, 8, 0, 0, 0);

  MwtPeriod *period2 = g_ptr_array_index (periods, 1);
  GDateTime *period2_start = mwt_period_get_start (period2);
  GDateTime *period2_end = mwt_period_get_end (period2);
  g_assert_true (g_date_time_equal (period2_start, period2_start_expected));
  g_assert_true (g_date_time_equal (period2_end, period2_end_expected));
  g_assert_cmpint (mwt_period_get_repeat_type (period2), ==, MWT_PERIOD_REPEAT_WEEK);
  g_assert_cmpuint (mwt_period_get_repeat_period (period2), ==, 1);
  g_assert_cmpuint (mwt_period_get_capacity_limit (period2), ==, G_MAXUINT64);
}

/* Test loading a simple tariff from a serialised #GVariant works. It’s not a
 * very exciting tariff. */
static void
test_tariff_loader_simple_variant (void)
{
  g_autoptr(GError) local_error = NULL;

  const gchar *variant_str =
    "('Mogwai tariff', @q 2, <@(sa(ttssqut)) ('a', [(0, 1, 'UTC', 'UTC', 0, 0, 0)])>)";
  g_autoptr(GVariant) variant = g_variant_parse (NULL, variant_str, NULL, NULL, &local_error);
  g_assert_no_error (local_error);

  g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();

  gboolean retval = mwt_tariff_loader_load_from_variant (loader, variant, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  MwtTariff *tariff = mwt_tariff_loader_get_tariff (loader);
  g_assert_true (MWT_IS_TARIFF (tariff));

  /* Check properties. */
  g_assert_cmpstr (mwt_tariff_get_name (tariff), ==, "a");

  GPtrArray *periods = mwt_tariff_get_periods (tariff);
  g_assert_cmpuint (periods->len, ==, 1);

  g_autoptr(GDateTime) period1_start_expected = g_date_time_new_utc (1970, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) period1_end_expected = g_date_time_new_utc (1970, 1, 1, 0, 0, 1);

  MwtPeriod *period1 = g_ptr_array_index (periods, 0);
  GDateTime *period1_start = mwt_period_get_start (period1);
  GDateTime *period1_end = mwt_period_get_end (period1);
  g_assert_true (g_date_time_equal (period1_start, period1_start_expected));
  g_assert_true (g_date_time_equal (period1_end, period1_end_expected));
  g_assert_cmpint (mwt_period_get_repeat_type (period1), ==, MWT_PERIOD_REPEAT_NONE);
  g_assert_cmpuint (mwt_period_get_repeat_period (period1), ==, 0);
  g_assert_cmpuint (mwt_period_get_capacity_limit (period1), ==, 0);
}

/* Test that loading a tariff with no periods fails gracefully.
 * Note: These bytes are in version 1 format. */
static void
test_tariff_loader_empty (void)
{
  const guint8 *data =
    (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                     "\x00\x01\x00\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                     "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                     "\x74\x71\x75\x74\x29\x29\x0e";
  const gsize len = 46;

  g_autoptr(GBytes) bytes = g_bytes_new_static (data, len);
  g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();
  g_autoptr(GError) error = NULL;

  gboolean retval = mwt_tariff_loader_load_from_bytes (loader, bytes, &error);
  g_assert_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID);
  g_assert_false (retval);

  MwtTariff *tariff = mwt_tariff_loader_get_tariff (loader);
  g_assert_null (tariff);
}

/* Test that loading a byteswapped tariff with no periods also fails.
 * Note: These bytes are in version 1 format. */
static void
test_tariff_loader_empty_byteswapped (void)
{
  const guint8 *data =
    (const guint8 *) "\x4d\x6f\x67\x77\x61\x69\x20\x74\x61\x72\x69\x66\x66"
                     "\x00\x00\x01\x74\x65\x73\x74\x2d\x74\x61\x72\x69\x66"
                     "\x66\x00\x00\x00\x00\x00\x0c\x00\x28\x73\x61\x28\x74"
                     "\x74\x71\x75\x74\x29\x29\x0e";
  const gsize len = 46;

  g_autoptr(GBytes) bytes = g_bytes_new_static (data, len);
  g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();
  g_autoptr(GError) error = NULL;

  gboolean retval = mwt_tariff_loader_load_from_bytes (loader, bytes, &error);
  g_assert_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID);
  g_assert_false (retval);

  MwtTariff *tariff = mwt_tariff_loader_get_tariff (loader);
  g_assert_null (tariff);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/tariff-loader/unloaded", test_tariff_loader_unloaded);
  g_test_add_func ("/tariff-loader/errors/bytes", test_tariff_loader_errors_bytes);
  g_test_add_func ("/tariff-loader/errors/variant", test_tariff_loader_errors_variant);
  g_test_add_func ("/tariff-loader/simple/bytes", test_tariff_loader_simple_bytes);
  g_test_add_func ("/tariff-loader/simple/variant", test_tariff_loader_simple_variant);
  g_test_add_func ("/tariff-loader/empty", test_tariff_loader_empty);
  g_test_add_func ("/tariff-loader/empty/byteswapped",
                   test_tariff_loader_empty_byteswapped);

  return g_test_run ();
}
