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
#include <libmogwai-schedule/tests/clock-dummy.h>
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
  MwsClock *clock;  /* (owned) */
  MwsScheduler *scheduler;  /* (owned) */
  MwsSignalLogger *scheduler_signals;  /* (owned) */
} Fixture;

typedef struct
{
  guint max_active_entries;  /* > 1 */
} TestData;

static void
setup (Fixture       *fixture,
       gconstpointer  test_data)
{
  const TestData *data = test_data;

  g_assert (data->max_active_entries > 0);

  fixture->connection_monitor = MWS_CONNECTION_MONITOR (mws_connection_monitor_dummy_new ());
  fixture->peer_manager = MWS_PEER_MANAGER (mws_peer_manager_dummy_new (FALSE));
  fixture->clock = MWS_CLOCK (mws_clock_dummy_new ());

  /* Construct the scheduler manually so we can set max-active-entries. */
  fixture->scheduler = g_object_new (MWS_TYPE_SCHEDULER,
                                     "connection-monitor", fixture->connection_monitor,
                                     "peer-manager", fixture->peer_manager,
                                     "clock", fixture->clock,
                                     "max-active-entries", data->max_active_entries,
                                     NULL);
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
  g_clear_object (&fixture->clock);
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

  g_autoptr(MwsClock) clock = MWS_CLOCK (mws_clock_dummy_new ());

  g_autoptr(MwsScheduler) scheduler = mws_scheduler_new (connection_monitor,
                                                         peer_manager,
                                                         clock);

  /* Do something to avoid the compiler warning about unused variables. */
  g_assert_true (mws_scheduler_get_peer_manager (scheduler) == peer_manager);
}

/* Assert the signal emissions from #MwsScheduler are correct for a single call
 * to mws_scheduler_update_entries().
 *
 * Two arrays of entries expected to be signalled as removed in a
 * #MwsScheduler::active-entries-changed signal must be provided:
 *  * @expected_changed_active_removed1 for the entries signalled before
 *    #MwsScheduler::entries-changed is emitted
 *  * expected_changed_active_removed2 for the entries signalled afterwards
 */
static void
assert_entries_changed_signals (Fixture   *fixture,
                                GPtrArray *expected_changed_added,
                                GPtrArray *expected_changed_removed,
                                GPtrArray *expected_changed_active_added,
                                GPtrArray *expected_changed_active_removed1,
                                GPtrArray *expected_changed_active_removed2)
{
  /* Squash empty arrays. */
  if (expected_changed_added != NULL && expected_changed_added->len == 0)
    expected_changed_added = NULL;
  if (expected_changed_removed != NULL && expected_changed_removed->len == 0)
    expected_changed_removed = NULL;
  if (expected_changed_active_added != NULL && expected_changed_active_added->len == 0)
    expected_changed_active_added = NULL;
  if (expected_changed_active_removed1 != NULL && expected_changed_active_removed1->len == 0)
    expected_changed_active_removed1 = NULL;
  if (expected_changed_active_removed2 != NULL && expected_changed_active_removed2->len == 0)
    expected_changed_active_removed2 = NULL;

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
  if (expected_changed_active_removed1 != NULL)
    mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                           fixture->scheduler, "active-entries-changed",
                                           &changed_active_added1, &changed_active_removed1);
  if (expected_changed_added != NULL || expected_changed_removed != NULL)
    {
      mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                             fixture->scheduler, "notify::entries",
                                             NULL);
      mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                             fixture->scheduler, "entries-changed",
                                             &changed_added, &changed_removed);
    }
  if (expected_changed_active_added != NULL)
    mws_signal_logger_assert_emission_pop (fixture->scheduler_signals,
                                           fixture->scheduler, "active-entries-changed",
                                           &changed_active_added2, &changed_active_removed2);

  mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);

  assert_ptr_arrays_equal (changed_added, expected_changed_added);
  assert_ptr_arrays_equal (changed_removed, expected_changed_removed);
  assert_ptr_arrays_equal (changed_active_added1, NULL);
  assert_ptr_arrays_equal (changed_active_removed1, expected_changed_active_removed1);
  assert_ptr_arrays_equal (changed_active_added2, expected_changed_active_added);
  assert_ptr_arrays_equal (changed_active_removed2, expected_changed_active_removed2);
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
  assert_entries_changed_signals (fixture, added1, NULL, added1, NULL, NULL);

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
  assert_entries_changed_signals (fixture, NULL, expected_removed1, NULL, expected_removed1, NULL);

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
  assert_entries_changed_signals (fixture, added2, NULL, added_active2, NULL, NULL);

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
  assert_entries_changed_signals (fixture, NULL, expected_removed3, NULL, added_active2, NULL);
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
  assert_entries_changed_signals (fixture, added1, NULL, added_active1, NULL, NULL);

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
  assert_entries_changed_signals (fixture, NULL, removed1, added_active2, added_active1, NULL);

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
  assert_entries_changed_signals (fixture, NULL, removed2, NULL, added_active2, NULL);
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
  g_autoptr(MwsClock) clock = NULL;

  g_object_get (fixture->scheduler,
                "entries", &entries,
                "max-entries", &max_entries,
                "connection-monitor", &connection_monitor,
                "max-active-entries", &max_active_entries,
                "peer-manager", &peer_manager,
                "allow-downloads", &allow_downloads,
                "clock", &clock,
                NULL);

  g_assert_nonnull (entries);
  g_assert_cmpuint (max_entries, >, 0);
  g_assert_nonnull (connection_monitor);
  g_assert_cmpuint (max_active_entries, >, 0);
  g_assert_nonnull (peer_manager);
  g_assert_nonnull (clock);
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
  assert_entries_changed_signals (fixture, added, NULL, expected_active, NULL, NULL);

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
                                      expected_active, old_expected_active, NULL);
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

/* Test that the size of the set of active entries is limited by
 * #MwsScheduler:max-active-entries, and that only elements in that set are
 * mentioned in #MwsScheduler::active-entries-changed signals.
 *
 * Schedule two entries, then add two more with priorities such that one of them
 * will be scheduled in place of one of the original entries. Remove one of the
 * scheduled entries so one of the unscheduled entries is scheduled; then remove
 * the remaining unscheduled entry to check that it’s not mentioned in
 * ::active-entries-changed signals. */
static void
test_scheduler_scheduling_max_active_entries (Fixture       *fixture,
                                              gconstpointer  test_data)
{
  const TestData *data = test_data;
  g_assert (data->max_active_entries == 2);

  g_autoptr(GError) local_error = NULL;

  /* Add several entries. */
  g_autoptr(MwsScheduleEntry) entry1 = schedule_entry_new_with_priority (":owner.1", 5);
  g_autoptr(MwsScheduleEntry) entry2 = schedule_entry_new_with_priority (":owner.1", 10);

  /* Set up the peer credentials. */
  mws_peer_manager_dummy_set_peer_credentials (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                               ":owner.1", "/some/owner");

  /* Add all the entries to the scheduler. */
  g_autoptr(GPtrArray) added1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added1, entry1);
  g_ptr_array_add (added1, entry2);

  g_autoptr(GPtrArray) expected_active1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active1, entry2);
  g_ptr_array_add (expected_active1, entry1);

  mws_scheduler_update_entries (fixture->scheduler, added1, NULL, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, added1, NULL, expected_active1, NULL, NULL);

  /* Add some more entries, one of which at a higher priority so that one of the
   * old ones is descheduled. */
  g_autoptr(MwsScheduleEntry) entry3 = schedule_entry_new_with_priority (":owner.1", 15);
  g_autoptr(MwsScheduleEntry) entry4 = schedule_entry_new_with_priority (":owner.1", 7);

  g_autoptr(GPtrArray) added2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added2, entry3);
  g_ptr_array_add (added2, entry4);

  g_autoptr(GPtrArray) expected_active2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active2, entry3);

  g_autoptr(GPtrArray) expected_inactive1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_inactive1, entry1);

  mws_scheduler_update_entries (fixture->scheduler, added2, NULL, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, added2, NULL, expected_active2, NULL, expected_inactive1);

  /* Remove one of the high priority entries so another should be scheduled in
   * its place. */
  g_autoptr(GPtrArray) removed_ids1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed_ids1, mws_schedule_entry_get_id (entry3));
  g_autoptr(GPtrArray) removed1 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed1, entry3);

  g_autoptr(GPtrArray) expected_active3 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active3, entry4);

  g_autoptr(GPtrArray) expected_inactive2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_inactive2, entry3);

  mws_scheduler_update_entries (fixture->scheduler, NULL, removed_ids1, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, NULL, removed1, expected_active3, expected_inactive2, NULL);

  /* Now remove an inactive entry and check it’s not signalled. */
  g_autoptr(GPtrArray) removed_ids2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed_ids2, mws_schedule_entry_get_id (entry1));
  g_autoptr(GPtrArray) removed2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed2, entry1);

  mws_scheduler_update_entries (fixture->scheduler, NULL, removed_ids2, &local_error);
  g_assert_no_error (local_error);
  assert_entries_changed_signals (fixture, NULL, removed2, NULL, NULL, NULL);
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
  assert_entries_changed_signals (fixture, added, NULL, expected_active1, NULL, NULL);

  /* Make `:owner.1` vanish, and check that both its entries (but not the third
   * entry) disappear. */
  mws_peer_manager_dummy_remove_peer (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                      ":owner.1");

  g_autoptr(GPtrArray) expected_removed = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_removed, entry1);
  g_ptr_array_add (expected_removed, entry2);

  g_autoptr(GPtrArray) expected_active2 = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (expected_active2, entry3);

  assert_entries_changed_signals (fixture, NULL, expected_removed,
                                  expected_active2, expected_active1, NULL);

  /* Now make `:owner.2` vanish, to test what happens when there will be no new
   * active entry to take the place of the one being removed. */
  mws_peer_manager_dummy_remove_peer (MWS_PEER_MANAGER_DUMMY (fixture->peer_manager),
                                      ":owner.2");

  assert_entries_changed_signals (fixture, NULL, expected_active2, NULL, expected_active2, NULL);
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
                                      NULL, NULL);

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
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array, NULL);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL, NULL);

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
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array, NULL);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL, NULL);

      /* Clean up. */
      teardown (fixture, test_data);
      setup (fixture, test_data);
    }
}

/* Test the transitions between different tariffs, checking whether they cause a
 * single entry to be scheduled/unscheduled appropriately. Each test vector
 * provides two states of a set of tariffs, and two times. Either the set of
 * tariffs can vary, or the time can vary in a single vector — not both. The
 * test starts in state 1 at time 1, checks whether the entry is scheduled
 * appropriately, transitions to state 2 at time 2, checks again, and then
 * transitions back to state 1 at time 1 to test the reverse transition (and
 * checks again). */
static void
test_scheduler_scheduling_tariff (Fixture       *fixture,
                                  gconstpointer  test_data)
{
  /* A tariff which is normally unmetered, but has a period from 01:00–02:00
   * each day which has zero capacity limit. */
  g_autoptr(GPtrArray) tariff1_periods = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) tariff1_period1_start = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) tariff1_period1_end = g_date_time_new_utc (2018, 1, 2, 0, 0, 0);
  g_autoptr(MwtPeriod) tariff1_period1 = mwt_period_new (tariff1_period1_start,
                                                         tariff1_period1_end,
                                                         MWT_PERIOD_REPEAT_DAY,
                                                         1,
                                                         "capacity-limit", G_MAXUINT64,
                                                         NULL);
  g_ptr_array_add (tariff1_periods, tariff1_period1);

  g_autoptr(GDateTime) tariff1_period2_start = g_date_time_new_utc (2018, 1, 1, 1, 0, 0);
  g_autoptr(GDateTime) tariff1_period2_end = g_date_time_new_utc (2018, 1, 1, 2, 0, 0);
  g_autoptr(MwtPeriod) tariff1_period2 = mwt_period_new (tariff1_period2_start,
                                                         tariff1_period2_end,
                                                         MWT_PERIOD_REPEAT_DAY,
                                                         1,
                                                         "capacity-limit", 0,
                                                         NULL);
  g_ptr_array_add (tariff1_periods, tariff1_period2);

  g_autoptr(MwtTariff) tariff1 = mwt_tariff_new ("tariff1", tariff1_periods);

  /* A tariff which is always unmetered (infinite capacity limit). */
  g_autoptr(GPtrArray) tariff_unmetered_periods = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) tariff_unmetered_period1_start = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) tariff_unmetered_period1_end = g_date_time_new_utc (2018, 1, 2, 0, 0, 0);
  g_autoptr(MwtPeriod) tariff_unmetered_period1 = mwt_period_new (tariff_unmetered_period1_start,
                                                                  tariff_unmetered_period1_end,
                                                                  MWT_PERIOD_REPEAT_DAY,
                                                                  1,
                                                                  "capacity-limit", G_MAXUINT64,
                                                                  NULL);
  g_ptr_array_add (tariff_unmetered_periods, tariff_unmetered_period1);

  g_autoptr(MwtTariff) tariff_unmetered = mwt_tariff_new ("tariff_unmetered",
                                                          tariff_unmetered_periods);

  /* A tariff which is normally metered, but has a period from 01:30–02:00
   * each day which has unlimited capacity. */
  g_autoptr(GPtrArray) tariff2_periods = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) tariff2_period1_start = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) tariff2_period1_end = g_date_time_new_utc (2018, 1, 2, 0, 0, 0);
  g_autoptr(MwtPeriod) tariff2_period1 = mwt_period_new (tariff2_period1_start,
                                                         tariff2_period1_end,
                                                         MWT_PERIOD_REPEAT_DAY,
                                                         1,
                                                         "capacity-limit", 0,
                                                         NULL);
  g_ptr_array_add (tariff2_periods, tariff2_period1);

  g_autoptr(GDateTime) tariff2_period2_start = g_date_time_new_utc (2018, 1, 1, 1, 30, 0);
  g_autoptr(GDateTime) tariff2_period2_end = g_date_time_new_utc (2018, 1, 1, 2, 30, 0);
  g_autoptr(MwtPeriod) tariff2_period2 = mwt_period_new (tariff2_period2_start,
                                                         tariff2_period2_end,
                                                         MWT_PERIOD_REPEAT_DAY,
                                                         1,
                                                         "capacity-limit", G_MAXUINT64,
                                                         NULL);
  g_ptr_array_add (tariff2_periods, tariff2_period2);

  g_autoptr(MwtTariff) tariff2 = mwt_tariff_new ("tariff2", tariff2_periods);

  /* A tariff has a single period and no recurrence. */
  g_autoptr(GPtrArray) tariff3_periods = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GDateTime) tariff3_period1_start = g_date_time_new_utc (2018, 1, 1, 0, 0, 0);
  g_autoptr(GDateTime) tariff3_period1_end = g_date_time_new_utc (2018, 1, 2, 0, 0, 0);
  g_autoptr(MwtPeriod) tariff3_period1 = mwt_period_new (tariff3_period1_start,
                                                         tariff3_period1_end,
                                                         MWT_PERIOD_REPEAT_NONE,
                                                         0,
                                                         "capacity-limit", 0,
                                                         NULL);
  g_ptr_array_add (tariff3_periods, tariff3_period1);

  g_autoptr(MwtTariff) tariff3 = mwt_tariff_new ("tariff3", tariff3_periods);

  g_autoptr(GError) local_error = NULL;
  struct
    {
      /* We use a fixed limit of 3 tariffs in these tests, because we should
       * be able to simulate any condition we care about using 3. Typically,
       * systems will have only 1 active connection (maybe 2 if they have wired
       * and Wi-Fi enabled at the same time). Each tariff listed here will
       * result in a separate network connection being configured.
       *
       * The dates/times below are given in UTC, and then converted to the
       * timezone given as @state1_tz or @state2_tz. This means they represent
       * the same instant as the UTC time, and hence @state1_expected_active
       * and @state2_expected_active are independent of @state1_tz and
       * @state2_tz. */
      const MwtTariff *state1_tariffs[3];
      gint state1_year;
      gint state1_month;
      gint state1_day;
      gint state1_hour;
      gint state1_minute;
      gdouble state1_seconds;
      const gchar *state1_tz;
      gboolean state1_expected_active;
      const MwtTariff *state2_tariffs[3];
      gint state2_year;
      gint state2_month;
      gint state2_day;
      gint state2_hour;
      gint state2_minute;
      gdouble state2_seconds;
      const gchar *state2_tz;
      gboolean state2_expected_active;
    }
  transitions[] =
    {
      /* Transition from an unmetered period to a metered period in the same
       * tariff. */
      { { tariff1, }, 2018, 2, 3, 17, 0, 0, NULL, TRUE,
        { tariff1, }, 2018, 2, 4, 1, 30, 0, NULL, FALSE },
      /* Transition within an unmetered period, and within a metered period. */
      { { tariff1, }, 2018, 2, 3, 17, 0, 0, NULL, TRUE,
        { tariff1, }, 2018, 2, 3, 17, 30, 0, NULL, TRUE },
      { { tariff1, }, 2018, 2, 4, 1, 15, 0, NULL, FALSE,
        { tariff1, }, 2018, 2, 4, 1, 30, 0, NULL, FALSE },
      /* Transition between an unmetered tariff and a normal tariff, for a time
       * which is and is not in the tariff metered period. */
      { { tariff_unmetered, }, 2018, 2, 3, 17, 0, 0, NULL, TRUE,
        { tariff1, }, 2018, 2, 3, 17, 0, 0, NULL, TRUE },
      { { tariff_unmetered, }, 2018, 2, 4, 1, 30, 0, NULL, TRUE,
        { tariff1, }, 2018, 2, 4, 1, 30, 0, NULL, FALSE },
      /* Transition from an unmetered period and a metered period in one
       * tariff, to a metered period in one and an unmetered period in another.
       * Since the scheduler requires *all* connections to be safe in order to
       * start a download, this leads to very limited active downloads. */
      { { tariff1, tariff2, }, 2018, 2, 4, 0, 30, 0, NULL, FALSE,
        { tariff1, tariff2, }, 2018, 2, 4, 1, 15, 0, NULL, FALSE },
      { { tariff1, tariff2, }, 2018, 2, 4, 1, 45, 0, NULL, FALSE,
        { tariff1, tariff2, }, 2018, 2, 4, 1, 59, 0, NULL, FALSE },
      { { tariff1, tariff2, }, 2018, 2, 4, 2, 0, 0, NULL, TRUE,
        { tariff1, tariff2, }, 2018, 2, 4, 2, 15, 0, NULL, TRUE },
      /* Timezone change in metered and unmetered periods. The underlying UTC
       * time may change at the same time, to either give a timezone change only
       * (where the underlying UTC time is the same) or a timezone and time
       * change such that the resolved wall clock time stays the same */
      /* Change timezone only: */
      { { tariff1, }, 2018, 2, 3, 15, 30, 0, "Europe/London" /* UTC+0 */, TRUE,
        { tariff1, }, 2018, 2, 3, 15, 30, 0, "Australia/Brisbane" /* UTC+10 */, TRUE },
      /* Keep wall clock time unchanged: */
      { { tariff1, }, 2018, 2, 3, 1, 30, 0, "Australia/Brisbane" /* UTC+10 */, FALSE,
        { tariff1, }, 2018, 2, 3, 11, 30, 0, "Europe/London" /* UTC+0 */, TRUE },

      { { tariff1, }, 2018, 2, 3, 16, 30, 0, "Europe/London" /* UTC+0 */, TRUE,
        { tariff1, }, 2018, 2, 3, 16, 30, 0, "Europe/Berlin" /* UTC+1 */, TRUE },
      { { tariff1, }, 2018, 2, 3, 15, 30, 0, "Europe/Berlin" /* UTC+1 */, TRUE,
        { tariff1, }, 2018, 2, 3, 16, 30, 0, "Europe/London" /* UTC+0 */, TRUE },

      { { tariff1, }, 2018, 2, 3, 1, 30, 0, "Europe/London" /* UTC+0 */, FALSE,
        { tariff1, }, 2018, 2, 3, 1, 30, 0, "Australia/Brisbane" /* UTC+10 */, FALSE },
      { { tariff1, }, 2018, 2, 2, 15, 30, 0, "Australia/Brisbane" /* UTC+10 */, TRUE,
        { tariff1, }, 2018, 2, 3, 1, 30, 0, "Europe/London" /* UTC+0 */, FALSE },

      { { tariff1, }, 2018, 2, 3, 1, 30, 0, "Europe/London", FALSE,
        { tariff1, }, 2018, 2, 3, 1, 30, 0, "Europe/Isle_of_Man", FALSE },
      /* Test different times in (and out of) a tariff which doesn’t repeat. */
      { { tariff3, }, 2017, 12, 30, 0, 0, 0, NULL, TRUE,
        { tariff3, }, 2018, 1, 1, 1, 30, 0, NULL, FALSE },
      { { tariff3, }, 2018, 1, 1, 0, 0, 0, NULL, FALSE,
        { tariff3, }, 2018, 1, 2, 0, 0, 0, NULL, TRUE },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (transitions); i++)
    {
      /* Set up the dates/times. The date/time figures in the test vector are
       * given in UTC and converted to the stated timezone, so they represent
       * the same instant as the UTC time, but with a likely different wall
       * clock time. */
      g_autoptr(GTimeZone) state1_tz =
          (transitions[i].state1_tz != NULL) ? g_time_zone_new (transitions[i].state1_tz) :
                                               g_time_zone_new_utc ();
      g_autoptr(GDateTime) state1_time_utc = g_date_time_new_utc (transitions[i].state1_year,
                                                                  transitions[i].state1_month,
                                                                  transitions[i].state1_day,
                                                                  transitions[i].state1_hour,
                                                                  transitions[i].state1_minute,
                                                                  transitions[i].state1_seconds);
      g_autoptr(GDateTime) state1_time = g_date_time_to_timezone (state1_time_utc,
                                                                  state1_tz);

      g_autoptr(GTimeZone) state2_tz =
          (transitions[i].state2_tz != NULL) ? g_time_zone_new (transitions[i].state2_tz) :
                                               g_time_zone_new_utc ();
      g_autoptr(GDateTime) state2_time_utc = g_date_time_new_utc (transitions[i].state2_year,
                                                                  transitions[i].state2_month,
                                                                  transitions[i].state2_day,
                                                                  transitions[i].state2_hour,
                                                                  transitions[i].state2_minute,
                                                                  transitions[i].state2_seconds);
      g_autoptr(GDateTime) state2_time = g_date_time_to_timezone (state2_time_utc,
                                                                  state2_tz);

      /* We only care about transitions within a given connection in this test,
       * so the set of connections available in states 1 and 2 must be the same */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_tariffs); j++)
        g_assert ((transitions[i].state1_tariffs[j] == NULL) ==
                  (transitions[i].state2_tariffs[j] == NULL));
      /* Similarly, we can vary either the wall clock time, or the connection
       * tariff, between states 1 and 2 — but varying both would give too many
       * axes of freedom in the test. Ensure only one varies. */
      gboolean all_tariffs_equal = TRUE;
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_tariffs); j++)
        all_tariffs_equal =
            all_tariffs_equal &&
            transitions[i].state1_tariffs[j] == transitions[i].state2_tariffs[j];
      g_assert (all_tariffs_equal ||
                g_date_time_equal (state1_time, state2_time));
      /* We want a monotonic wall clock. */
      g_assert_cmpint (g_date_time_compare (state1_time, state2_time), <=, 0);

      g_test_message ("Transition test %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT,
                      i + 1, G_N_ELEMENTS (transitions));

      /* Set the time in state 1. */
      mws_clock_dummy_set_time_zone (MWS_CLOCK_DUMMY (fixture->clock), state1_tz);
      mws_clock_dummy_set_time (MWS_CLOCK_DUMMY (fixture->clock), state1_time);

      /* Set up the connections in state 1. */
      g_autoptr(GHashTable) state1_connections =
          g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_tariffs); j++)
        {
          if (transitions[i].state1_tariffs[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);

              g_autofree MwsConnectionDetails *connection = g_new0 (MwsConnectionDetails, 1);
              connection->metered = MWS_METERED_NO;
              connection->allow_downloads_when_metered = FALSE;
              connection->allow_downloads = TRUE;
              connection->tariff = transitions[i].state1_tariffs[j];

              g_hash_table_insert (state1_connections, g_steal_pointer (&id),
                                   g_steal_pointer (&connection));
            }
        }

      mws_connection_monitor_dummy_update_connections (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                       state1_connections, NULL);

      /* Due to the test setup, we always expect allow-downloads=true. */
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
                                      NULL, NULL);

      /* Change to the next time. */
      mws_clock_dummy_set_time_zone (MWS_CLOCK_DUMMY (fixture->clock), state2_tz);
      mws_clock_dummy_set_time (MWS_CLOCK_DUMMY (fixture->clock), state2_time);

      /* Change to the next connection state. */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state2_tariffs); j++)
        {
          if (transitions[i].state2_tariffs[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);

              g_autofree MwsConnectionDetails *connection = g_new0 (MwsConnectionDetails, 1);
              connection->metered = MWS_METERED_NO;
              connection->allow_downloads_when_metered = FALSE;
              connection->allow_downloads = TRUE;
              connection->tariff = transitions[i].state2_tariffs[j];

              mws_connection_monitor_dummy_update_connection (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                              id, connection);
            }
        }

      if (transitions[i].state1_expected_active == transitions[i].state2_expected_active)
        mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
      else if (transitions[i].state1_expected_active)
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array, NULL);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL, NULL);

      /* Change back to the previous time. This might not be very realistic,
       * but the scheduler should deal with it. */
      mws_clock_dummy_set_time_zone (MWS_CLOCK_DUMMY (fixture->clock), state1_tz);
      mws_clock_dummy_set_time (MWS_CLOCK_DUMMY (fixture->clock), state1_time);

      /* Change back to the previous connection state. The entry’s scheduled
       * state should always be the same as before. */
      for (gsize j = 0; j < G_N_ELEMENTS (transitions[i].state1_tariffs); j++)
        {
          if (transitions[i].state1_tariffs[j] != NULL)
            {
              g_autofree gchar *id = g_strdup_printf ("connection%" G_GSIZE_FORMAT, j);

              g_autofree MwsConnectionDetails *connection = g_new0 (MwsConnectionDetails, 1);
              connection->metered = MWS_METERED_NO;
              connection->allow_downloads_when_metered = FALSE;
              connection->allow_downloads = TRUE;
              connection->tariff = transitions[i].state1_tariffs[j];

              mws_connection_monitor_dummy_update_connection (MWS_CONNECTION_MONITOR_DUMMY (fixture->connection_monitor),
                                                              id, connection);
            }
        }

      if (transitions[i].state1_expected_active == transitions[i].state2_expected_active)
        mws_signal_logger_assert_no_emissions (fixture->scheduler_signals);
      else if (transitions[i].state2_expected_active)
        assert_entries_changed_signals (fixture, NULL, NULL, NULL, entry_array, NULL);
      else
        assert_entries_changed_signals (fixture, NULL, NULL, entry_array, NULL, NULL);

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

  const TestData standard_data =
    {
      .max_active_entries = 1,
    };
  const TestData max_active_entries_data =
    {
      .max_active_entries = 2,
    };

  g_test_add_func ("/scheduler/construction", test_scheduler_construction);
  g_test_add ("/scheduler/entries", Fixture, &standard_data, setup,
              test_scheduler_entries, teardown);
  g_test_add ("/scheduler/entries/remove-for-owner", Fixture,
              &standard_data, setup,
              test_scheduler_entries_remove_for_owner, teardown);
  g_test_add ("/scheduler/properties", Fixture, &standard_data, setup,
              test_scheduler_properties, teardown);
  g_test_add ("/scheduler/scheduling/entry-priorities", Fixture,
              &standard_data, setup,
              test_scheduler_scheduling_entry_priorities, teardown);
  g_test_add ("/scheduler/scheduling/peer-priorities", Fixture,
              &standard_data, setup,
              test_scheduler_scheduling_peer_priorities, teardown);
  g_test_add ("/scheduler/scheduling/max-active-entries", Fixture,
              &max_active_entries_data, setup,
              test_scheduler_scheduling_max_active_entries, teardown);
  g_test_add ("/scheduler/scheduling/peer-vanished", Fixture,
              &standard_data, setup,
              test_scheduler_scheduling_peer_vanished, teardown);
  g_test_add ("/scheduler/scheduling/metered-connection", Fixture,
              &standard_data, setup,
              test_scheduler_scheduling_metered_connection, teardown);
  g_test_add ("/scheduler/scheduling/tariff", Fixture,
              &standard_data, setup,
              test_scheduler_scheduling_tariff, teardown);

  return g_test_run ();
}
