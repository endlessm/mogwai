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
#include <libmogwai-tariff/tariff-builder.h>
#include <libmogwai-tariff/tariff.h>
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
  /* Clear the signal logger first so we don’t accidentally log signals from
   * the other objects while we finalise them. */
  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
  g_clear_pointer (&fixture->scheduler_signals, (GDestroyNotify) mws_signal_logger_free);

  g_clear_object (&fixture->scheduler);
  g_clear_object (&fixture->peer_manager);
  g_clear_object (&fixture->connection_monitor);
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
  if ((expected_changed_added != NULL && expected_changed_added->len > 0) ||
      (expected_changed_removed != NULL && expected_changed_removed->len > 0))
    {
      mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                             fixture->scheduler, "notify::entries",
                                             NULL);
      mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                             fixture->scheduler, "entries-changed",
                                             &changed_added, &changed_removed);
    }
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

/* Convenience method to create a new schedule entry and set its priority. */
static MwsScheduleEntry *
schedule_entry_new_with_priority (const gchar *owner,
                                  guint32      priority)
{
  g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (owner);
  mws_schedule_entry_set_priority (entry, priority);
  return g_steal_pointer (&entry);
}

/* Assert that when all the entries in @expected_scheduling_order are added to
 * the scheduler, they are scheduled in the given order. This is checked by
 * adding them all, checking which one entry is active, removing it, then
 * checking which entry is made active next, etc. This checks all signal
 * emissions. It requires the scheduler to be empty beforehand, and to have a
 * max-active-entries limit of 1. */
static void
assert_scheduling_order (Fixture                 *fixture,
                         const MwsScheduleEntry **expected_scheduling_order,
                         gsize                    n_entries)
{
  g_autoptr(GError) local_error = NULL;

  g_assert (n_entries > 0);
  g_assert (g_hash_table_size (mws_scheduler_get_entries (fixture->scheduler)) == 0);
  guint max_active_entries;
  g_object_get (fixture->scheduler, "max-active-entries", &max_active_entries, NULL);
  g_assert (max_active_entries == 1);

  /* Add all the entries to the scheduler. Add them in the reverse of the
   * expected scheduling order, just in case the scheduler is being really dumb. */
  g_autoptr(GPtrArray) added = g_ptr_array_new_with_free_func (NULL);
  for (gsize i = 0; i < n_entries; i++)
    g_ptr_array_add (added, expected_scheduling_order[n_entries - 1 - i]);

  g_autoptr(GPtrArray) expected_active = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active, expected_scheduling_order[0]);

  mws_scheduler_update_entries (fixture->scheduler, added, NULL, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, added, NULL, expected_active, NULL);

  /* Remove each active entry to work out what is going to be scheduled next. */
  for (gsize i = 0; i < n_entries; i++)
    {
      g_autoptr(GPtrArray) removed = g_ptr_array_new_with_free_func (NULL);
      g_ptr_array_add (removed, mws_schedule_entry_get_id (expected_scheduling_order[i]));

      g_autoptr(GPtrArray) expected_removed = g_ptr_array_new_with_free_func (NULL);
      g_ptr_array_add (expected_removed, expected_scheduling_order[i]);

      g_autoptr(GPtrArray) old_expected_active = g_steal_pointer (&expected_active);
      expected_active = g_ptr_array_new_with_free_func (NULL);
      if (i + 1 < n_entries)
        g_ptr_array_add (expected_active, expected_scheduling_order[i + 1]);

      mws_scheduler_update_entries (fixture->scheduler, NULL, removed, &local_error);
      g_assert_no_error (local_error);
      assert_entries_changed_signals (fixture,
                                      NULL, expected_removed,
                                      expected_active, old_expected_active);
    }
}

/* Test that schedule entries are correctly ordered by their priority within a
 * single peer. */
static void
test_scheduler_scheduling_entry_priorities (Fixture       *fixture,
                                            gconstpointer  test_data)
{
  /* Add several entries. @entry4 and @entry5 have the same priority, but ties
   * are broken using the entry ID, so @entry5 loses as its ID is slightly
   * higher. */
  g_autoptr(MwsScheduleEntry) entry1 = schedule_entry_new_with_priority (":owner.1", 5);
  g_autoptr(MwsScheduleEntry) entry2 = schedule_entry_new_with_priority (":owner.1", 10);
  g_autoptr(MwsScheduleEntry) entry3 = schedule_entry_new_with_priority (":owner.1", 15);
  g_autoptr(MwsScheduleEntry) entry4 = schedule_entry_new_with_priority (":owner.1", 16);
  g_autoptr(MwsScheduleEntry) entry5 = schedule_entry_new_with_priority (":owner.1", 16);

  /* We expect @entry4 to be scheduled first, then @entry5, etc. */
  const MwsScheduleEntry *expected_scheduling_order[] = { entry4, entry5, entry3, entry2, entry1 };

  assert_scheduling_order (fixture, expected_scheduling_order,
                           G_N_ELEMENTS (expected_scheduling_order));
}

/* Test that schedule entries are correctly ordered by their priority between
 * multiple peers. */
static void
test_scheduler_scheduling_peer_priorities (Fixture       *fixture,
                                           gconstpointer  test_data)
{
  /* Add several entries. */
  g_autoptr(MwsScheduleEntry) entry1 = schedule_entry_new_with_priority (":eos.updater", 5);
  g_autoptr(MwsScheduleEntry) entry2 = schedule_entry_new_with_priority (":gnome.software", 10);
  g_autoptr(MwsScheduleEntry) entry3 = schedule_entry_new_with_priority (":eos.updater", 15);
  g_autoptr(MwsScheduleEntry) entry4 = schedule_entry_new_with_priority (":random.program.1", 12);
  g_autoptr(MwsScheduleEntry) entry5 = schedule_entry_new_with_priority (":random.program.1", 100);
  g_autoptr(MwsScheduleEntry) entry6 = schedule_entry_new_with_priority (":random.program.2", 2);
  g_autoptr(MwsScheduleEntry) entry7 = schedule_entry_new_with_priority (":unknown.peer", 110);

  /* Set up the peer credentials. */
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":eos.updater", "/usr/bin/eos-updater");
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":gnome.software", "/usr/bin/gnome-software");
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":random.program.1", "/some/random/path1");
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":random.program.2", "/some/random/path2");

  /* We expect @entry3 to be scheduled first, then @entry2, etc. Note that since
   * the priority order of :random.program.1 and :random.program.2 is undefined
   * (depends on the hash of their unique names, so may change between GLib
   * versions), [entry6] and [entry5, entry4] may change places at some point
   * in the future. */
  const MwsScheduleEntry *expected_scheduling_order[] =
      { entry3, entry2, entry1, entry6, entry5, entry4, entry7 };

  assert_scheduling_order (fixture, expected_scheduling_order,
                           G_N_ELEMENTS (expected_scheduling_order));
}

/* Test that the schedule entries for a given peer are automatically removed if
 * that peer vanishes, whether they are active or not. */
static void
test_scheduler_scheduling_peer_vanished (Fixture       *fixture,
                                         gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;

  /* Add several entries. */
  g_autoptr(MwsScheduleEntry) entry1 = schedule_entry_new_with_priority (":owner.1", 5);
  g_autoptr(MwsScheduleEntry) entry2 = schedule_entry_new_with_priority (":owner.1", 10);
  g_autoptr(MwsScheduleEntry) entry3 = schedule_entry_new_with_priority (":owner.2", 2);

  /* Set up the peer credentials. Make sure :owner.1 is scheduled first, by
   * making it gnome-software. */
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":owner.1", "/usr/bin/gnome-software");
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":owner.2", "/some/other/path");

  /* Add all the entries to the scheduler. */
  g_autoptr(GPtrArray) added = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added, entry1);
  g_ptr_array_add (added, entry2);
  g_ptr_array_add (added, entry3);

  g_autoptr(GPtrArray) expected_active1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active1, entry2);

  mws_scheduler_update_entries (fixture->scheduler, added, NULL, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, added, NULL, expected_active1, NULL);

  /* Make `:owner.1` vanish, and check that both its entries (but not the third
   * entry) disappear. */
  mws_peer_manager_dummy_remove_peer (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                      ":owner.1");

  g_autoptr(GPtrArray) expected_removed = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_removed, entry1);
  g_ptr_array_add (expected_removed, entry2);

  g_autoptr(GPtrArray) expected_active2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active2, entry3);

  assert_entries_changed_signals (fixture, NULL, expected_removed, expected_active2, expected_active1);

  /* Now make `:owner.2` vanish, to test what happens when there will be no new
   * active entry to take the place of the one being removed. */
  mws_peer_manager_dummy_remove_peer (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                      ":owner.2");

  assert_entries_changed_signals (fixture, NULL, expected_active2, NULL, expected_active2);
}

/* Test the transitions between different network connection states, checking
 * whether they cause a single entry to be scheduled/unscheduled
 * appropriately. Each test vector provides two states of a set of network
 * connections. The test starts in state 1, checks whether the entry is
 * scheduled appropriately, transitions to state 2, checks again, and then
 * transitions back to state 1 to test the reverse transition (and checks
 * again). */
static void
test_scheduler_scheduling_metered_connection (Fixture       *fixture,
                                              gconstpointer  test_data)
{
  /* Various pre-defined connection details, which can be assigned for use below. */
  const MwsConnectionDetails connection_metered =
    {
      .metered = MWS_METERED_YES,
      .allow_downloads_when_metered = FALSE,
      .allow_downloads = TRUE,
      .tariff = NULL,
    };
  const MwsConnectionDetails connection_maybe_metered =
    {
      .metered = MWS_METERED_GUESS_YES,
      .allow_downloads_when_metered = FALSE,
      .allow_downloads = TRUE,
      .tariff = NULL,
    };
  const MwsConnectionDetails connection_unmetered =
    {
      .metered = MWS_METERED_NO,
      .allow_downloads_when_metered = FALSE,
      .allow_downloads = TRUE,
      .tariff = NULL,
    };
  const MwsConnectionDetails connection_metered_allow_downloads =
    {
      .metered = MWS_METERED_YES,
      .allow_downloads_when_metered = TRUE,
      .allow_downloads = TRUE,
      .tariff = NULL,
    };
  const MwsConnectionDetails connection_metered_no_downloads =
    {
      .metered = MWS_METERED_YES,
      .allow_downloads_when_metered = FALSE,
      .allow_downloads = FALSE,
      .tariff = NULL,
    };
  const MwsConnectionDetails connection_unmetered_no_downloads =
    {
      .metered = MWS_METERED_NO,
      .allow_downloads_when_metered = FALSE,
      .allow_downloads = FALSE,
      .tariff = NULL,
    };

  g_autoptr(GError) local_error = NULL;
  struct
    {
      /* We use a fixed limit of 3 connections in these tests, because we should
       * be able to simulate any condition we care about using 3. Typically,
       * systems will have only 1 active connection (maybe 2 if they have wired
       * and Wi-Fi enabled at the same time). */
      const MwsConnectionDetails *state1_connections[3];
      gboolean state1_expected_allow_downloads;
      gboolean state1_expected_active;
      const MwsConnectionDetails *state2_connections[3];
      gboolean state2_expected_allow_downloads;
      gboolean state2_expected_active;
    }
  transitions[] =
    {
      /* Transition from definitely metered to definitely unmetered. */
      { { &connection_metered, }, TRUE, FALSE,
        { &connection_unmetered, }, TRUE, TRUE },
      /* Transition from maybe metered to definitely unmetered. */
      { { &connection_maybe_metered, }, TRUE, FALSE,
        { &connection_unmetered, }, TRUE, TRUE },
      /* Transition from maybe metered to definitely unmetered. */
      { { &connection_maybe_metered, }, TRUE, FALSE,
        { &connection_unmetered, }, TRUE, TRUE },
      /* Transition from definitely metered to definitely unmetered, but with
       * downloads disabled. */
      { { &connection_metered_no_downloads, }, FALSE, FALSE,
        { &connection_unmetered_no_downloads, }, FALSE, FALSE },
      /* Transition from unmetered to metered (but with downloads allowed). */
      { { &connection_unmetered, }, TRUE, TRUE,
        { &connection_metered_allow_downloads, }, TRUE, TRUE },
      /* Transition from metered to metered (but with downloads allowed). */
      { { &connection_metered, }, TRUE, FALSE,
        { &connection_metered_allow_downloads, }, TRUE, TRUE },
      /* Transition from two definitely metered connections to having both of
       * them definitely unmetered. */
      { { &connection_metered, &connection_metered }, TRUE, FALSE,
        { &connection_unmetered, &connection_unmetered, }, TRUE, TRUE },
      /* Transition from two definitely metered connections to having one of
       * them definitely unmetered. */
      { { &connection_metered, &connection_metered }, TRUE, FALSE,
        { &connection_metered, &connection_unmetered, }, TRUE, FALSE },
      /* Transition from a metered and an unmetered connection to having one of
       * them allow downloads. */
      { { &connection_unmetered, &connection_metered }, TRUE, FALSE,
        { &connection_unmetered, &connection_metered_allow_downloads, }, TRUE, TRUE },
      /* Transition from a selection of connections to various unmetered ones
       * with downloads disallowed. allow-downloads must become false. */
      { { &connection_unmetered, &connection_metered, &connection_unmetered }, TRUE, FALSE,
        { &connection_unmetered_no_downloads, &connection_unmetered_no_downloads, &connection_unmetered_no_downloads }, FALSE, FALSE },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (transitions); i++)
    {
      /* We only care about transitions within a given connection in this test,
       * so the set of connections available in states 1 and 2 must be the same */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_connections); j++)
        g_assert ((transitions[i].state1_connections[j] == NULL) ==
                  (transitions[i].state2_connections[j] == NULL));

      g_test_message ("Transition test %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT,
                      i + 1, G_N_ELEMENTS (transitions));

      /* Set up the connections in state 1. */
      g_autoptr(GHashTable) state1_connections =
          g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_connections); j++)
        {
          if (transitions[i].state1_connections[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);
              g_hash_table_insert (state1_connections, g_steal_pointer (&id),
                                   transitions[i].state1_connections[j]);
            }
        }

      mws_connection_monitor_dummy_update_connections (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                       state1_connections, NULL);

      if (transitions[i].state1_expected_allow_downloads)
        mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                               fixture->scheduler,
                                               "notify::allow-downloads", NULL);
      mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

      /* Add a single entry to the scheduler. */
      g_autoptr(MwsScheduleEntry) entry = mws_schedule_entry_new (":owner.1");

      g_autoptr(GPtrArray) entry_array = g_ptr_array_new_with_free_func (NULL);
      g_ptr_array_add (entry_array, entry);

      mws_scheduler_update_entries (fixture->scheduler, entry_array, NULL, &local_error);
      g_assert_no_error (local_error);
      assert_entries_changed_signals (fixture, entry_array, NULL,
                                      (transitions[i].state1_expected_active ? entry_array : NULL),
                                      NULL);

      /* Change to the next connection state. */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state2_connections); j++)
        {
          if (transitions[i].state2_connections[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);
              mws_connection_monitor_dummy_update_connection (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                              id, transitions[i].state2_connections[j]);
            }
        }

      if (transitions[i].state2_expected_allow_downloads != transitions[i].state1_expected_allow_downloads)
        mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                               fixture->scheduler,
                                               "notify::allow-downloads", NULL);

      if (transitions[i].state1_expected_active == transitions[i].state2_expected_active)
        mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
      else if (transitions[i].state1_expected_active)
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL);

      /* Change back to the previous connection state. The entry’s scheduled
       * state should always be the same as before. */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_connections); j++)
        {
          if (transitions[i].state1_connections[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);
              mws_connection_monitor_dummy_update_connection (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                              id, transitions[i].state1_connections[j]);
            }
        }

      if (transitions[i].state1_expected_allow_downloads != transitions[i].state2_expected_allow_downloads)
        mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                               fixture->scheduler,
                                               "notify::allow-downloads", NULL);

      if (transitions[i].state1_expected_active == transitions[i].state2_expected_active)
        mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
      else if (transitions[i].state2_expected_active)
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL);

      /* Clean up. */
      teardown (fixture, test_data);
      setup (fixture, test_data);
    }
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
  g_test_add ("/scheduler/scheduling/entry-priorities", Fixture, NULL, setup,
              test_scheduler_scheduling_entry_priorities, teardown);
  g_test_add ("/scheduler/scheduling/peer-priorities", Fixture, NULL, setup,
              test_scheduler_scheduling_peer_priorities, teardown);
  g_test_add ("/scheduler/scheduling/peer-vanished", Fixture, NULL, setup,
              test_scheduler_scheduling_peer_vanished, teardown);
  g_test_add ("/scheduler/scheduling/metered-connection", Fixture, NULL, setup,
              test_scheduler_scheduling_metered_connection, teardown);

  return g_test_run ();
}
