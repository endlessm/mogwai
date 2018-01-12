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
  GDBusConnection *connection;  /* (owned) */
} Fixture;

static void
setup (Fixture       *fixture,
       gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  fixture->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
}

static void
teardown (Fixture       *fixture,
          gconstpointer  test_data)
{
  g_clear_object (&fixture->connection);
}

static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

/* Test constructing an #MwscScheduleEntry object with invalid arguments. */
static void
test_service_construction_error (Fixture       *fixture,
                                 gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) error = NULL;

  mwsc_schedule_entry_new_async (fixture->connection,
                                 "com.endlessm.MogwaiSchedule1.Nonexistent",
                                 "/com/endlessm/DownloadManager1/Nonexistent",
                                 NULL,  /* cancellable */
                                 async_result_cb,
                                 &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  g_autoptr(MwscScheduleEntry) entry = NULL;
  entry = mwsc_schedule_entry_new_finish (result, &error);
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

  g_test_add ("/schedule-entry/construction/error", Fixture, NULL, setup,
              test_service_construction_error, teardown);

  return g_test_run ();
}
