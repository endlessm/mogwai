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
#include <libmogwai-schedule-client/schedule-entry.h>
#include <locale.h>


typedef struct
{
  GTestDBus *bus;  /* (owned) */
  GDBusConnection *connection;  /* (owned) */
} Fixture;

static void
setup (Fixture       *fixture,
       gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  fixture->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (fixture->bus);

  fixture->connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (fixture->bus),
                                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                                NULL,
                                                                NULL,
                                                                &error);
  g_assert_no_error (error);
}

static void
teardown (Fixture       *fixture,
          gconstpointer  test_data)
{
  if (fixture->connection != NULL)
    g_dbus_connection_close_sync (fixture->connection, NULL, NULL);
  g_clear_object (&fixture->connection);

  g_test_dbus_down (fixture->bus);
  g_clear_object (&fixture->bus);
}

static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

/* Test asynchronously constructing an #MwscScheduleEntry object with invalid
 * arguments. */
static void
test_service_construction_async_error (Fixture       *fixture,
                                       gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) error = NULL;

  mwsc_schedule_entry_new_full_async (fixture->connection,
                                      "com.endlessm.MogwaiSchedule1.Nonexistent",
                                      "/com/endlessm/DownloadManager1/Nonexistent",
                                      NULL,  /* cancellable */
                                      async_result_cb,
                                      &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  g_autoptr(MwscScheduleEntry) entry = NULL;
  entry = mwsc_schedule_entry_new_full_finish (result, &error);
  g_assert_error (error, MWSC_SCHEDULE_ENTRY_ERROR,
                  MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY);
  g_assert_null (entry);
}

/* Test synchronously constructing an #MwscScheduleEntry object with invalid
 * arguments. */
static void
test_service_construction_sync_error (Fixture       *fixture,
                                      gconstpointer  test_data)
{
  g_autoptr(MwscScheduleEntry) entry = NULL;
  g_autoptr(GError) error = NULL;

  entry = mwsc_schedule_entry_new_full (fixture->connection,
                                        "com.endlessm.MogwaiSchedule1.Nonexistent",
                                        "/com/endlessm/DownloadManager1/Nonexistent",
                                        NULL,  /* cancellable */
                                        &error);
  g_assert_error (error, MWSC_SCHEDULE_ENTRY_ERROR,
                  MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY);
  g_assert_null (entry);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/schedule-entry/construction/async/error", Fixture, NULL, setup,
              test_service_construction_async_error, teardown);
  g_test_add ("/schedule-entry/construction/sync/error", Fixture, NULL, setup,
              test_service_construction_sync_error, teardown);

  return g_test_run ();
}