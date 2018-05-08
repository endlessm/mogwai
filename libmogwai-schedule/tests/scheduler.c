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
#include <libmogwai-schedule/tests/connection-monitor-dummy.h>
#include <libmogwai-schedule/tests/peer-manager-dummy.h>
#include <libmogwai-schedule/tests/signal-logger.h>
#include <locale.h>


/* Fixture which creates a #MwsScheduler, connects it to a mock peer manager,
 * and logs all its signal emissions in @scheduled_signals.
 *
 * @scheduler_signals must be empty when the fixture is torn down. */
typedef struct
{
  MwsConnectionMonitor *connection_monitor;  /* (owned) */
  MwsPeerManager *peer_manager;  /* (owned) */
  MwsScheduler *scheduler;  /* (owned) */
  MwsSignalLogger *scheduler_signals;  /* (owned) */
} Fixture;

static void
setup (Fixture       *fixture,
       gconstpointer  test_data)
{
  fixture->connection_monitor = MWS_CONNECTION_MONITOR (mws_connection_monitor_dummy_new ());
  fixture->peer_manager = MWS_PEER_MANAGER (mws_peer_manager_dummy_new (FALSE));

  fixture->scheduler = mws_scheduler_new (fixture->connection_monitor,
                                          fixture->peer_manager);
  fixture->scheduler_signals = mws_signal_logger_new ();
  mws_signal_logger_connect (fixture->scheduler_signals,
                             fixture->scheduler, "notify::allow-downloads");
  mws_signal_logger_connect (fixture->scheduler_signals,
                             fixture->scheduler, "notify::entries");
  mws_signal_logger_connect (fixture->scheduler_signals,
                             fixture->scheduler, "entries-changed");
  mws_signal_logger_connect (fixture->scheduler_signals,
                             fixture->scheduler, "active-entries-changed");
}

static void
teardown (Fixture       *fixture,
          gconstpointer  test_data)
{
  g_clear_object (&fixture->scheduler);
  g_clear_object (&fixture->peer_manager);
  g_clear_object (&fixture->connection_monitor);

  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
  g_clear_pointer (&fixture->scheduler_signals, (GDestroyNotify) mws_signal_logger_free);
}

/**
 * assert_ptr_arrays_equal:
 * @array1: (nullable): an array
 * @array2: (nullable): another array
 *
 * Compare @array1 and @array2, checking that all their elements are pointerwise
 * equal.
 *
 * Passing %NULL to either parameter is equivalent to passing an empty array.
 *
 * If the arrays do not match, an assertion will fail.
 *
 * Since: 0.1.0
 */
static void
assert_ptr_arrays_equal (GPtrArray *array1,
                         GPtrArray *array2)
{
  gboolean array1_is_empty = (array1 == NULL || array1->len == 0);
  gboolean array2_is_empty = (array2 == NULL || array2->len == 0);

  g_assert_cmpuint (array1_is_empty, ==, array2_is_empty);

  if (array1_is_empty || array2_is_empty)
    return;

  g_assert_cmpuint (array1->len, ==, array2->len);

  for (gsize i = 0; i < array1->len; i++)
    g_assert_true (g_ptr_array_index (array1, i) == g_ptr_array_index (array2, i));
}

/* Test that constructing a #MwsScheduler works. A basic smoketest. */
static void
test_scheduler_construction (void)
{
  g_autoptr(MwsConnectionMonitor) connection_monitor = NULL;
  connection_monitor = MWS_CONNECTION_MONITOR (mws_connection_monitor_dummy_new ());

  g_autoptr(MwsPeerManager) peer_manager = NULL;
  peer_manager = MWS_PEER_MANAGER (mws_peer_manager_dummy_new (FALSE));

  g_autoptr(MwsScheduler) scheduler = mws_scheduler_new (connection_monitor,
                                                         peer_manager);

  /* Do something to avoid the compiler warning about unused variables. */
  g_assert_true (mws_scheduler_get_peer_manager (scheduler) == peer_manager);
}

/* Assert the signal emissions from #MwsScheduler are correct for a single call
 * to mws_scheduler_update_entries(). */
static void
assert_entries_changed_signals (Fixture   *fixture,
                                GPtrArray *expected_changed_added,
                                GPtrArray *expected_changed_removed,
                                GPtrArray *expected_changed_active_added,
                                GPtrArray *expected_changed_active_removed)
{
  g_autoptr(GPtrArray) changed_added = NULL;
  g_autoptr(GPtrArray) changed_removed = NULL;
  g_autoptr(GPtrArray) changed_active_added1 = NULL;
  g_autoptr(GPtrArray) changed_active_removed1 = NULL;
  g_autoptr(GPtrArray) changed_active_added2 = NULL;
  g_autoptr(GPtrArray) changed_active_removed2 = NULL;

  /* We expect active-entries-changed to be emitted first for removed active
   * entries (if there are any); then notify::entries and entries-changed; then
   * active-entries-changed *again* for added active entries (if there are
   * any). */
  if (expected_changed_active_removed != NULL && expected_changed_active_removed->len > 0)
    mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                           fixture->scheduler, "active-entries-changed",
                                           &changed_active_added1, &changed_active_removed1);
  mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                         fixture->scheduler, "notify::entries",
                                         NULL);
  mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                         fixture->scheduler, "entries-changed",
                                         &changed_added, &changed_removed);
  if (expected_changed_active_added != NULL && expected_changed_active_added->len > 0)
    mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                           fixture->scheduler, "active-entries-changed",
                                           &changed_active_added2, &changed_active_removed2);

  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  assert_ptr_arrays_equal (changed_added, expected_changed_added);
  assert_ptr_arrays_equal (changed_removed, expected_changed_removed);
  assert_ptr_arrays_equal (changed_active_added1, NULL);
  assert_ptr_arrays_equal (changed_active_removed1, expected_changed_active_removed);
  assert_ptr_arrays_equal (changed_active_added2, expected_changed_active_added);
  assert_ptr_arrays_equal (changed_active_removed2, NULL);
}

/* Test that entries are added to and removed from the scheduler correctly. */
static void
test_scheduler_entries (Fixture       *fixture,
                        gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  gboolean success;
  GHashTable *entries;

  /* Check that it can be a no-op. */
  success = mws_scheduler_update_entries (fixture->scheduler, NULL, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 0);

  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  /* Add an entry. */
  g_autoptr(GPtrArray) added1 = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (added1, mws_schedule_entry_new (":owner.1"));

  success = mws_scheduler_update_entries (fixture->scheduler, added1, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 1);
  assert_entries_changed_signals (fixture, added1, NULL, added1, NULL);

  MwsScheduleEntry *entry = mws_scheduler_get_entry (fixture->scheduler, "0");
  g_assert_nonnull (entry);
  g_assert_true (MWS_IS_SCHEDULE_ENTRY (entry));
  g_assert_true (mws_scheduler_is_entry_active (fixture->scheduler, entry));

  /* Remove an entry. */
  g_autoptr(GPtrArray) removed1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed1, mws_schedule_entry_get_id (entry));
  g_autoptr(GPtrArray) expected_removed1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_removed1, entry);

  success = mws_scheduler_update_entries (fixture->scheduler, NULL, removed1, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 0);
  assert_entries_changed_signals (fixture, NULL, expected_removed1, NULL, expected_removed1);

  /* Remove a non-existent entry. */
  g_autoptr(GPtrArray) removed2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed2, "nope");

  success = mws_scheduler_update_entries (fixture->scheduler, NULL, removed2, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 0);
  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  /* Add several entries. */
  g_autoptr(GPtrArray) added2 = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (added2, mws_schedule_entry_new (":owner.1"));
  g_ptr_array_add (added2, mws_schedule_entry_new (":owner.1"));
  g_ptr_array_add (added2, mws_schedule_entry_new (":owner.2"));

  g_autoptr(GPtrArray) added_active2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added_active2, added2->pdata[0]);

  success = mws_scheduler_update_entries (fixture->scheduler, added2, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 3);
  assert_entries_changed_signals (fixture, added2, NULL, added_active2, NULL);

  /* Add duplicate entry. */
  g_autoptr(GPtrArray) added3 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added3, added2->pdata[0]);

  success = mws_scheduler_update_entries (fixture->scheduler, added3, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 3);
  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  /* No-op when non-empty. */
  success = mws_scheduler_update_entries (fixture->scheduler, NULL, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 3);
  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  /* Remove several entries. */
  g_autoptr(GPtrArray) removed3 = g_ptr_array_new_with_free_func (NULL);
  g_autoptr(GPtrArray) expected_removed3 = g_ptr_array_new_with_free_func (NULL);

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init (&iter, entries);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_ptr_array_add (removed3, key);
      g_ptr_array_add (expected_removed3, value);
    }

  success = mws_scheduler_update_entries (fixture->scheduler, NULL, removed3, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 0);
  assert_entries_changed_signals (fixture, NULL, expected_removed3, NULL, added_active2);
}

/* Test that entries can be removed by owner from a scheduler correctly. */
static void
test_scheduler_entries_remove_for_owner (Fixture       *fixture,
                                         gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  gboolean success;
  GHashTable *entries;

  /* Add several entries. */
  g_autoptr(GPtrArray) added1 = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (added1, mws_schedule_entry_new (":owner.1"));
  g_ptr_array_add (added1, mws_schedule_entry_new (":owner.1"));
  g_ptr_array_add (added1, mws_schedule_entry_new (":owner.2"));

  g_autoptr(GPtrArray) added_active1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added_active1, added1->pdata[0]);

  success = mws_scheduler_update_entries (fixture->scheduler, added1, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 3);
  assert_entries_changed_signals (fixture, added1, NULL, added_active1, NULL);

  /* Remove all entries from one owner, including the active entry. */
  success = mws_scheduler_remove_entries_for_owner (fixture->scheduler, ":owner.1", &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  g_autoptr(GPtrArray) removed1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed1, added1->pdata[0]);
  g_ptr_array_add (removed1, added1->pdata[1]);

  g_autoptr(GPtrArray) removed_active1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed_active1, added1->pdata[0]);

  g_autoptr(GPtrArray) added_active2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added_active2, added1->pdata[2]);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 1);
  assert_entries_changed_signals (fixture, NULL, removed1, added_active2, added_active1);

  /* Remove entries from a non-existent owner. */
  success = mws_scheduler_remove_entries_for_owner (fixture->scheduler, ":owner.100", &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 1);
  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  /* Remove the remaining entries. */
  success = mws_scheduler_remove_entries_for_owner (fixture->scheduler, ":owner.2", &local_error);
  g_assert_no_error (local_error);
  g_assert_true (success);

  g_autoptr(GPtrArray) removed2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed2, added1->pdata[2]);

  entries = mws_scheduler_get_entries (fixture->scheduler);
  g_assert_cmpuint (g_hash_table_size (entries), ==, 0);
  assert_entries_changed_signals (fixture, NULL, removed2, NULL, added_active2);
}

/* Test that getting the properties from a #MwsScheduler works. */
static void
test_scheduler_properties (Fixture       *fixture,
                           gconstpointer  test_data)
{
  g_autoptr(GHashTable) entries = NULL;
  guint max_entries;
  g_autoptr(MwsConnectionMonitor) connection_monitor = NULL;
  guint max_active_entries;
  g_autoptr(MwsPeerManager) peer_manager = NULL;
  gboolean allow_downloads;

  g_object_get (fixture->scheduler,
                "entries", &entries,
                "max-entries", &max_entries,
                "connection-monitor", &connection_monitor,
                "max-active-entries", &max_active_entries,
                "peer-manager", &peer_manager,
                "allow-downloads", &allow_downloads,
                NULL);

  g_assert_nonnull (entries);
  g_assert_cmpuint (max_entries, >, 0);
  g_assert_nonnull (connection_monitor);
  g_assert_cmpuint (max_active_entries, >, 0);
  g_assert_nonnull (peer_manager);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/scheduler/construction", test_scheduler_construction);
  g_test_add ("/scheduler/entries", Fixture, NULL, setup,
              test_scheduler_entries, teardown);
  g_test_add ("/scheduler/entries/remove-for-owner", Fixture, NULL, setup,
              test_scheduler_entries_remove_for_owner, teardown);
  g_test_add ("/scheduler/properties", Fixture, NULL, setup,
              test_scheduler_properties, teardown);

  return g_test_run ();
}
