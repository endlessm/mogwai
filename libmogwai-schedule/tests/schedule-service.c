/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017, 2018 Endless Mobile, Inc.
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
#include <libmogwai-schedule/connection-monitor.h>
#include <libmogwai-schedule/peer-manager.h>
#include <libmogwai-schedule/scheduler.h>
#include <libmogwai-schedule/schedule-service.h>
#include <libmogwai-schedule/service.h>
#include <libmogwai-schedule/clock-system.h>
#include <libmogwai-schedule/tests/connection-monitor-dummy.h>
#include <libmogwai-schedule/tests/peer-manager-dummy.h>
#include <locale.h>


static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

/* A test fixture which creates an in-process #MwsScheduleService and all the
 * associated state management, a #GDBusConnection for it, and a
 * #GDBusConnection for the client. Both connections are on a private
 * #GTestDBus instance. */
typedef struct
{
  GTestDBus *bus;  /* (owned) */
  GDBusConnection *server_connection;  /* (owned) */
  GDBusConnection *client_connection;  /* (owned) */
  MwsConnectionMonitor *connection_monitor;  /* (owned) */
  MwsPeerManager *peer_manager;  /* (owned) */
  MwsClock *clock;  /* (owned) */
  MwsScheduler *scheduler;  /* (owned) */
  MwsScheduleService *service;  /* (owned) */
} BusFixture;

static void
bus_setup (BusFixture    *fixture,
           gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  /* Set up a dummy bus and two connections to it. */
  fixture->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (fixture->bus);

  fixture->server_connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (fixture->bus),
                                                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                                                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                       NULL, NULL,
                                                                       &local_error);
  g_assert_no_error (local_error);

  fixture->client_connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (fixture->bus),
                                                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                                                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                       NULL, NULL,
                                                                       &local_error);
  g_assert_no_error (local_error);

  fixture->connection_monitor = MWS_CONNECTION_MONITOR (mws_connection_monitor_dummy_new ());
  fixture->peer_manager = MWS_PEER_MANAGER (mws_peer_manager_dummy_new (FALSE));
  fixture->clock = MWS_CLOCK (mws_clock_system_new ());

  /* Set some credentials for the first peer so calls don’t fail by default. We
   * can override this later by calling set_fail(). */
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":1.1", "/some/peer/path");

  /* Construct the scheduler manually so we can set max-entries. */
  fixture->scheduler = g_object_new (MWS_TYPE_SCHEDULER,
                                     "connection-monitor", fixture->connection_monitor,
                                     "peer-manager", fixture->peer_manager,
                                     "clock", fixture->clock,
                                     "max-entries", 10,
                                     NULL);

  fixture->service = mws_schedule_service_new (fixture->server_connection, "/test",
                                               fixture->scheduler);

  g_assert_true (mws_schedule_service_register (fixture->service, &local_error));
  g_assert_no_error (local_error);
}

static void
bus_teardown (BusFixture    *fixture,
              gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  mws_schedule_service_unregister (fixture->service);
  g_clear_object (&fixture->service);

  g_clear_object (&fixture->scheduler);
  g_clear_object (&fixture->clock);
  g_clear_object (&fixture->peer_manager);
  g_clear_object (&fixture->connection_monitor);

  g_dbus_connection_close_sync (fixture->client_connection, NULL, &local_error);
  g_assert_no_error (local_error);
  g_clear_object (&fixture->client_connection);

  g_dbus_connection_close_sync (fixture->server_connection, NULL, &local_error);
  g_assert_no_error (local_error);
  g_clear_object (&fixture->server_connection);

  g_test_dbus_down (fixture->bus);
  g_clear_object (&fixture->bus);
}

/* Helper function to synchronously call a D-Bus method on the scheduler using
 * the #BusFixture.client_connection. */
static GVariant *
scheduler_call_method (BusFixture    *fixture,
                       const gchar   *method_name,
                       GVariant      *parameters,
                       GVariantType  *reply_type,
                       GError       **error)
{
  g_assert (reply_type != NULL);

  g_autoptr(GAsyncResult) result = NULL;
  g_dbus_connection_call (fixture->client_connection,
                          g_dbus_connection_get_unique_name (fixture->server_connection),
                          "/test",
                          "com.endlessm.DownloadManager1.Scheduler",
                          method_name, parameters, reply_type,
                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                          1000, NULL, async_result_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  return g_dbus_connection_call_finish (fixture->client_connection,
                                        result, error);
}

/* Test a normal call to Schedule() succeeds. */
static void
test_service_dbus_schedule (BusFixture    *fixture,
                            gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_path_variant = NULL;

  entry_path_variant = scheduler_call_method (fixture, "Schedule",
                                              g_variant_new ("(a{sv})", NULL),
                                              G_VARIANT_TYPE ("(o)"),
                                              &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (entry_path_variant);

  const gchar *entry_path;
  g_variant_get (entry_path_variant, "(&o)", &entry_path);

  g_assert_cmpstr (entry_path, !=, "");
}

/* Test that invalid parameters to Schedule() return an error. */
static void
test_service_dbus_schedule_invalid_parameters (BusFixture    *fixture,
                                               gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_path_variant = NULL;

  entry_path_variant = scheduler_call_method (fixture, "Schedule",
                                              g_variant_new_parsed ("({ 'resumable': <'not a boolean'> },)"),
                                              G_VARIANT_TYPE ("(o)"),
                                              &local_error);
  g_assert_error (local_error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_null (entry_path_variant);
}

/* Test that an invalid peer is rejected when it calls Schedule(). */
static void
test_service_dbus_schedule_invalid_peer (BusFixture    *fixture,
                                         gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_path_variant = NULL;

  /* Tell the dummy peer manager to fail to get the details for any peer. */
  mws_peer_manager_dummy_set_fail (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager), TRUE);

  entry_path_variant = scheduler_call_method (fixture, "Schedule",
                                              g_variant_new ("(a{sv})", NULL),
                                              G_VARIANT_TYPE ("(o)"),
                                              &local_error);
  g_assert_error (local_error, MWS_SCHEDULER_ERROR, MWS_SCHEDULER_ERROR_IDENTIFYING_PEER);
  g_assert_null (entry_path_variant);
}

/* Test a normal call to ScheduleEntries() succeeds. The number of entries to
 * create is passed in @test_data. */
static void
test_service_dbus_schedule_entries (BusFixture    *fixture,
                                    gconstpointer  test_data)
{
  gsize n_entries = GPOINTER_TO_UINT (test_data);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_paths_variant = NULL;

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

  g_assert (n_entries > 0);
  for (gsize i = 0; i < n_entries; i++)
    g_variant_builder_add (&builder, "a{sv}", NULL);

  entry_paths_variant = scheduler_call_method (fixture, "ScheduleEntries",
                                               g_variant_new ("(aa{sv})",
                                                              &builder),
                                               G_VARIANT_TYPE ("(ao)"),
                                               &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (entry_paths_variant);

  g_autoptr(GVariantIter) entries_paths_iter = NULL;
  const gchar *entry_path;
  g_autoptr(GHashTable) entries_paths_set = g_hash_table_new_full (g_str_hash,
                                                                   g_str_equal,
                                                                   NULL, NULL);

  g_variant_get (entry_paths_variant, "(ao)", &entries_paths_iter);
  while (g_variant_iter_loop (entries_paths_iter, "&o", &entry_path))
    {
      g_assert_cmpstr (entry_path, !=, "");
      g_assert_false (g_hash_table_contains (entries_paths_set, entry_path));
      g_hash_table_add (entries_paths_set, entry_path);
    }

  g_assert_cmpuint (g_hash_table_size (entries_paths_set), ==, n_entries);
}

/* Test that ScheduleEntries() rejects entries with invalid parameters. The
 * number of entries to create is passed in @test_data. */
static void
test_service_dbus_schedule_entries_invalid_parameters (BusFixture    *fixture,
                                                       gconstpointer  test_data)
{
  gsize n_entries = GPOINTER_TO_UINT (test_data);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_paths_variant = NULL;

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

  g_assert (n_entries > 0);
  for (gsize i = 0; i < n_entries; i++)
    g_variant_builder_add_parsed (&builder, "{ 'priority': <'not a uint32'> }");

  entry_paths_variant = scheduler_call_method (fixture, "ScheduleEntries",
                                               g_variant_new ("(aa{sv})",
                                                              &builder),
                                               G_VARIANT_TYPE ("(ao)"),
                                               &local_error);
  g_assert_error (local_error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_null (entry_paths_variant);
}

/* Test that ScheduleEntries() rejects entries from peers it can’t identify. The
 * number of entries to create is passed in @test_data. */
static void
test_service_dbus_schedule_entries_invalid_peer (BusFixture    *fixture,
                                                 gconstpointer  test_data)
{
  gsize n_entries = GPOINTER_TO_UINT (test_data);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_paths_variant = NULL;

  /* Tell the dummy peer manager to fail to get the details for any peer. */
  mws_peer_manager_dummy_set_fail (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager), TRUE);

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

  g_assert (n_entries > 0);
  for (gsize i = 0; i < n_entries; i++)
    g_variant_builder_add (&builder, "a{sv}", NULL);

  entry_paths_variant = scheduler_call_method (fixture, "ScheduleEntries",
                                               g_variant_new ("(aa{sv})",
                                                              &builder),
                                               G_VARIANT_TYPE ("(ao)"),
                                               &local_error);
  g_assert_error (local_error, MWS_SCHEDULER_ERROR, MWS_SCHEDULER_ERROR_IDENTIFYING_PEER);
  g_assert_null (entry_paths_variant);
}

/* Test that ScheduleEntries() rejects entries which would take it over the
 * scheduler’s limit on entries. */
static void
test_service_dbus_schedule_entries_full (BusFixture    *fixture,
                                         gconstpointer  test_data)
{
  guint max_entries;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) entry_paths_variant = NULL;

  /* bus_setup() already sets a low limit on the number of entries. */
  g_object_get (G_OBJECT (fixture->scheduler), "max-entries", &max_entries, NULL);
  g_test_message ("max-entries: %u", max_entries);

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

  for (gsize i = 0; i <= max_entries; i++)
    g_variant_builder_add (&builder, "a{sv}", NULL);

  entry_paths_variant = scheduler_call_method (fixture, "ScheduleEntries",
                                               g_variant_new ("(aa{sv})",
                                                              &builder),
                                               G_VARIANT_TYPE ("(ao)"),
                                               &local_error);
  g_assert_error (local_error, MWS_SCHEDULER_ERROR, MWS_SCHEDULER_ERROR_FULL);
  g_assert_null (entry_paths_variant);
}

/* Test that Hold() and Release() affect mws_schedule_service_get_busy(). */
static void
test_service_dbus_hold_normal (BusFixture    *fixture,
                               gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  /* The schedule service shouldn’t be busy to begin with. */
  g_assert_false (mws_schedule_service_get_busy (fixture->service));

  /* Hold it. */
  g_autoptr(GVariant) unit_variant1 = NULL;
  unit_variant1 = scheduler_call_method (fixture, "Hold",
                                         g_variant_new ("(s)", "Test reason"),
                                         G_VARIANT_TYPE_UNIT,
                                         &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (unit_variant1);

  /* Should be busy now. */
  g_assert_true (mws_schedule_service_get_busy (fixture->service));

  /* Release it. */
  g_autoptr(GVariant) unit_variant2 = NULL;
  unit_variant2 = scheduler_call_method (fixture, "Release",
                                         NULL,  /* no arguments */
                                         G_VARIANT_TYPE_UNIT,
                                         &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (unit_variant2);

  /* Should not be busy again. */
  g_assert_false (mws_schedule_service_get_busy (fixture->service));
}

/* Test that calling Hold() twice results in an error. */
static void
test_service_dbus_hold_twice (BusFixture    *fixture,
                              gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  /* The schedule service shouldn’t be busy to begin with. */
  g_assert_false (mws_schedule_service_get_busy (fixture->service));

  /* Hold it. */
  g_autoptr(GVariant) unit_variant1 = NULL;
  unit_variant1 = scheduler_call_method (fixture, "Hold",
                                         g_variant_new ("(s)", "Test reason"),
                                         G_VARIANT_TYPE_UNIT,
                                         &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (unit_variant1);

  /* Should be busy now. */
  g_assert_true (mws_schedule_service_get_busy (fixture->service));

  /* Hold it again; this should error. */
  g_autoptr(GVariant) unit_variant2 = NULL;
  unit_variant2 = scheduler_call_method (fixture, "Hold",
                                         g_variant_new ("(s)", "Test reason 2"),
                                         G_VARIANT_TYPE_UNIT,
                                         &local_error);
  g_assert_error (local_error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED);
  g_assert_null (unit_variant2);

  /* Should still be busy now. */
  g_assert_true (mws_schedule_service_get_busy (fixture->service));
}

/* Test that calling Release() twice (or once without calling Hold() results in
 * an error. */
static void
test_service_dbus_release_twice (BusFixture    *fixture,
                                 gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  /* The schedule service shouldn’t be busy to begin with. */
  g_assert_false (mws_schedule_service_get_busy (fixture->service));

  /* Release it. */
  g_autoptr(GVariant) unit_variant1 = NULL;
  unit_variant1 = scheduler_call_method (fixture, "Release",
                                         NULL,  /* no arguments */
                                         G_VARIANT_TYPE_UNIT,
                                         &local_error);
  g_assert_error (local_error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED);
  g_assert_null (unit_variant1);

  /* Should still not be busy. */
  g_assert_false (mws_schedule_service_get_busy (fixture->service));
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/schedule-service/dbus/schedule", BusFixture, NULL, bus_setup,
              test_service_dbus_schedule, bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule/invalid-parameters",
              BusFixture, NULL, bus_setup,
              test_service_dbus_schedule_invalid_parameters, bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule/invalid-peer",
              BusFixture, NULL, bus_setup,
              test_service_dbus_schedule_invalid_peer, bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule-entries/single", BusFixture, GUINT_TO_POINTER (1),
              bus_setup, test_service_dbus_schedule_entries, bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule-entries/three", BusFixture, GUINT_TO_POINTER (3),
              bus_setup, test_service_dbus_schedule_entries, bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule-entries/invalid-parameters",
              BusFixture, GUINT_TO_POINTER (3),
              bus_setup, test_service_dbus_schedule_entries_invalid_parameters,
              bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule-entries/invalid-peer",
              BusFixture, GUINT_TO_POINTER (3),
              bus_setup, test_service_dbus_schedule_entries_invalid_peer,
              bus_teardown);
  g_test_add ("/schedule-service/dbus/schedule-entries/full", BusFixture, NULL,
              bus_setup, test_service_dbus_schedule_entries_full, bus_teardown);
  g_test_add ("/schedule-service/dbus/hold/normal", BusFixture, NULL,
              bus_setup, test_service_dbus_hold_normal, bus_teardown);
  g_test_add ("/schedule-service/dbus/hold/twice", BusFixture, NULL,
              bus_setup, test_service_dbus_hold_twice, bus_teardown);
  g_test_add ("/schedule-service/dbus/release/twice", BusFixture, NULL,
              bus_setup, test_service_dbus_release_twice, bus_teardown);

  return g_test_run ();
}
