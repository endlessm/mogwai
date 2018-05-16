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
#include <libmogwai-schedule/scheduler.h>
#include <libmogwai-schedule/schedule-entry.h>
#include <libmogwai-schedule/tests/signal-logger.h>
#include <locale.h>


/* Test that constructing a #MwsScheduleEntry works. A basic smoketest. */
static void
test_schedule_entry_construction (void)
{
  g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (":owner.1");

  g_assert_nonnull (entry);
  g_assert_true (MWS_IS_SCHEDULE_ENTRY (entry));

  g_assert_nonnull (mws_schedule_entry_get_id (entry));
  g_assert_cmpstr (mws_schedule_entry_get_id (entry), !=, "");
  g_assert_cmpstr (mws_schedule_entry_get_owner (entry), ==, ":owner.1");
  g_assert_false (mws_schedule_entry_get_resumable (entry));
  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 0);
}

/* Test that constructing a complete #MwsScheduleEntry from a #GVariant works. */
static void
test_schedule_entry_construction_variant (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(MwsScheduleEntry) entry = NULL;
  entry = mws_schedule_entry_new_from_variant (":owner.1",
                                               g_variant_new_parsed (
                                                 "@a{sv} {"
                                                   "'resumable': <@b true>,"
                                                   "'priority': <@u 5>"
                                                 "}"),
                                               &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (entry);
  g_assert_true (MWS_IS_SCHEDULE_ENTRY (entry));

  g_assert_nonnull (mws_schedule_entry_get_id (entry));
  g_assert_cmpstr (mws_schedule_entry_get_id (entry), !=, "");
  g_assert_cmpstr (mws_schedule_entry_get_owner (entry), ==, ":owner.1");
  g_assert_true (mws_schedule_entry_get_resumable (entry));
  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 5);
}

/* Test that constructing a complete #MwsScheduleEntry from a %NULL #GVariant
 * works. */
static void
test_schedule_entry_construction_variant_null (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(MwsScheduleEntry) entry = NULL;
  entry = mws_schedule_entry_new_from_variant (":owner.1", NULL, &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (entry);
  g_assert_true (MWS_IS_SCHEDULE_ENTRY (entry));

  g_assert_nonnull (mws_schedule_entry_get_id (entry));
  g_assert_cmpstr (mws_schedule_entry_get_id (entry), !=, "");
  g_assert_cmpstr (mws_schedule_entry_get_owner (entry), ==, ":owner.1");
  g_assert_false (mws_schedule_entry_get_resumable (entry));
  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 0);
}

/* Test that constructing a complete #MwsScheduleEntry from a #GVariant works,
 * and an unknown entry in the #GVariant is ignored. */
static void
test_schedule_entry_construction_variant_unknown (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(MwsScheduleEntry) entry = NULL;
  entry = mws_schedule_entry_new_from_variant (":owner.1",
                                               g_variant_new_parsed (
                                                 "@a{sv} {"
                                                   "'resumable': <@b false>,"
                                                   "'priority': <@u 500>,"
                                                   "'unknown value': <@b true>"
                                                 "}"),
                                               &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (entry);
  g_assert_true (MWS_IS_SCHEDULE_ENTRY (entry));

  g_assert_nonnull (mws_schedule_entry_get_id (entry));
  g_assert_cmpstr (mws_schedule_entry_get_id (entry), !=, "");
  g_assert_cmpstr (mws_schedule_entry_get_owner (entry), ==, ":owner.1");
  g_assert_false (mws_schedule_entry_get_resumable (entry));
  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 500);
}

/* Test that constructing a complete #MwsScheduleEntry from a #GVariant fails
 * with an error if one of the #GVariant parameters is the wrong type. */
static void
test_schedule_entry_construction_variant_invalid_type (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(MwsScheduleEntry) entry = NULL;
  entry = mws_schedule_entry_new_from_variant (":owner.1",
                                               g_variant_new_parsed (
                                                 "@a{sv} {"
                                                   "'resumable': <@u 1000>,"
                                                   "'priority': <@u 500>"
                                                 "}"),
                                               &local_error);

  g_assert_error (local_error, MWS_SCHEDULER_ERROR,
                  MWS_SCHEDULER_ERROR_INVALID_PARAMETERS);
  g_assert_null (entry);
}

/* Check that newly constructed entries all have different IDs. */
static void
test_schedule_entry_different_ids (void)
{
  g_autoptr(MwsScheduleEntry) entry1 = mws_schedule_entry_new (":owner.1");
  g_autoptr(MwsScheduleEntry) entry2 = mws_schedule_entry_new (":owner.1");
  g_autoptr(MwsScheduleEntry) entry3 = mws_schedule_entry_new (":owner.1");

  const gchar *id1 = mws_schedule_entry_get_id (entry1);
  const gchar *id2 = mws_schedule_entry_get_id (entry2);
  const gchar *id3 = mws_schedule_entry_get_id (entry3);

  g_assert_cmpstr (id1, !=, id2);
  g_assert_cmpstr (id2, !=, id3);
  g_assert_cmpstr (id3, !=, id1);
}

/* Check that g_object_get() and g_object_set() work on #MwsScheduleEntry
 * properties. */
static void
test_schedule_entry_properties (void)
{
  g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (":owner.1");

  g_autofree gchar *id = NULL;
  g_autofree gchar *owner = NULL;
  gboolean resumable;
  guint32 priority;

  g_object_get (entry,
                "id", &id,
                "owner", &owner,
                "resumable", &resumable,
                "priority", &priority,
                NULL);

  g_assert_cmpstr (id, ==, mws_schedule_entry_get_id (entry));
  g_assert_cmpstr (owner, ==, mws_schedule_entry_get_owner (entry));
  g_assert_cmpstr (owner, ==, ":owner.1");
  g_assert_cmpuint (resumable, ==, mws_schedule_entry_get_resumable (entry));
  g_assert_cmpuint (priority, ==, mws_schedule_entry_get_priority (entry));

  g_autoptr(MwsSignalLogger) logger = mws_signal_logger_new ();
  mws_signal_logger_connect (logger, entry, "notify");

  g_object_set (entry,
                "resumable", !resumable,
                "priority", priority + 1,
                NULL);

  g_assert_cmpuint (mws_schedule_entry_get_resumable (entry), ==, !resumable);
  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, priority + 1);

  mws_signal_logger_assert_notify_emission_pop (logger, entry, "priority");
  mws_signal_logger_assert_notify_emission_pop (logger, entry, "resumable");
  mws_signal_logger_assert_no_emissions (logger);
}

/* Test getting and setting the priority property.. */
static void
test_schedule_entry_properties_priority (void)
{
  g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (":owner.1");

  g_autoptr(MwsSignalLogger) logger = mws_signal_logger_new ();
  mws_signal_logger_connect (logger, entry, "notify::priority");

  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 0);

  mws_schedule_entry_set_priority (entry, 0);
  mws_signal_logger_assert_no_emissions (logger);

  mws_schedule_entry_set_priority (entry, 5);
  mws_signal_logger_assert_notify_emission_pop (logger, entry, "priority");
  mws_signal_logger_assert_no_emissions (logger);

  g_assert_cmpuint (mws_schedule_entry_get_priority (entry), ==, 5);
}

/* Test getting and setting the resumable property.. */
static void
test_schedule_entry_properties_resumable (void)
{
  g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (":owner.1");

  g_autoptr(MwsSignalLogger) logger = mws_signal_logger_new ();
  mws_signal_logger_connect (logger, entry, "notify::resumable");

  g_assert_false (mws_schedule_entry_get_resumable (entry));

  mws_schedule_entry_set_resumable (entry, FALSE);
  mws_signal_logger_assert_no_emissions (logger);

  mws_schedule_entry_set_resumable (entry, TRUE);
  mws_signal_logger_assert_notify_emission_pop (logger, entry, "resumable");
  mws_signal_logger_assert_no_emissions (logger);

  g_assert_true (mws_schedule_entry_get_resumable (entry));
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/schedule-entry/construction",
                   test_schedule_entry_construction);
  g_test_add_func ("/schedule-entry/construction/variant",
                   test_schedule_entry_construction_variant);
  g_test_add_func ("/schedule-entry/construction/variant/null",
                   test_schedule_entry_construction_variant_null);
  g_test_add_func ("/schedule-entry/construction/variant/unknown",
                   test_schedule_entry_construction_variant_unknown);
  g_test_add_func ("/schedule-entry/construction/variant/invalid-type",
                   test_schedule_entry_construction_variant_invalid_type);
  g_test_add_func ("/schedule-entry/different-ids",
                   test_schedule_entry_different_ids);
  g_test_add_func ("/schedule-entry/properties",
                   test_schedule_entry_properties);
  g_test_add_func ("/schedule-entry/properties/priority",
                   test_schedule_entry_properties_priority);
  g_test_add_func ("/schedule-entry/properties/resumable",
                   test_schedule_entry_properties_resumable);

  return g_test_run ();
}
