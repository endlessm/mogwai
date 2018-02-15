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
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/schedule-entry.h>
#include <libmogwai-schedule/scheduler.h>


/* These errors do go over the bus, and are registered in schedule-service.c. */
G_DEFINE_QUARK (MwsSchedulerError, mws_scheduler_error)

/* Cached state for a schedule entry, including its current active state, and
 * any calculated state which is not trivially derivable from the properties of
 * the #MwsScheduleEntry itself. */
typedef struct
{
  gboolean is_active;
} EntryData;

static EntryData *entry_data_new  (void);
static void       entry_data_free (EntryData *data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EntryData, entry_data_free);

/* Create a new #EntryData struct with default values. */
static EntryData *
entry_data_new (void)
{
  g_autoptr(EntryData) data = g_new0 (EntryData, 1);
  return g_steal_pointer (&data);
}

static void
entry_data_free (EntryData *data)
{
  g_free (data);
}

static void mws_scheduler_dispose      (GObject      *object);

static void mws_scheduler_get_property (GObject      *object,
                                        guint         property_id,
                                        GValue        *value,
                                        GParamSpec   *pspec);
static void mws_scheduler_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec);

/**
 * MwsScheduler:
 *
 * A scheduler object which stores a set of #MwsScheduleEntrys and allows
 * managing them using bulk add and remove operations. It looks at their
 * properties and the current network status and schedules them appropriately.
 *
 * Since: 0.1.0
 */
struct _MwsScheduler
{
  GObject parent;

  /* Mapping from entry ID to (not nullable) entry. */
  GHashTable *entries;  /* (owned) (element-type utf8 MwsScheduleEntry) */
  gsize max_entries;

  /* Mapping from entry ID to (not nullable) entry data. We can’t use the same
   * hash table as @entries since we need to be able to return that one in
   * mws_scheduler_get_entries(). */
  GHashTable *entries_data;  /* (owned) (element-type utf8 EntryData) */
};

/* Arbitrarily chosen. */
static const gsize DEFAULT_MAX_ENTRIES = 1024;

typedef enum
{
  PROP_ENTRIES = 1,
  PROP_MAX_ENTRIES,
} MwsSchedulerProperty;

G_DEFINE_TYPE (MwsScheduler, mws_scheduler, G_TYPE_OBJECT)

static void
mws_scheduler_class_init (MwsSchedulerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_MAX_ENTRIES + 1] = { NULL, };

  object_class->dispose = mws_scheduler_dispose;
  object_class->get_property = mws_scheduler_get_property;
  object_class->set_property = mws_scheduler_set_property;

  /**
   * MwsScheduler:entries: (type GHashTable(utf8,MwsScheduleEntry)) (transfer none)
   *
   * Set of schedule entries known to the scheduler, which might be empty. It is
   * a mapping from entry ID to #MwsScheduleEntry instance. Use
   * mws_scheduler_update_entries() to modify the mapping.
   *
   * Since: 0.1.0
   */
  props[PROP_ENTRIES] =
      g_param_spec_boxed ("entries", "Entries",
                          "Set of schedule entries known to the scheduler.",
                          G_TYPE_HASH_TABLE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:max-entries:
   *
   * Maximum number of schedule entries which can be present in the scheduler at
   * any time. This is not a limit on the number of active schedule entries. It
   * exists to make explicit the avoidance of array bounds overflows in the
   * scheduler. It should be considered high enough to not be reached apart from
   * in exception circumstances.
   *
   * Since: 0.1.0
   */
  props[PROP_MAX_ENTRIES] =
      g_param_spec_uint ("max-entries", "Max. Entries",
                         "Maximum number of schedule entries present in the scheduler.",
                         0, G_MAXUINT, DEFAULT_MAX_ENTRIES,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /**
   * MwsScheduler::entries-changed:
   * @self: a #MwsScheduler
   * @added: (element-type MwsScheduleEntry) (nullable): potentially empty or
   *     %NULL array of added entries (a %NULL array is equivalent to an empty one)
   * @removed: (element-type MwsScheduleEntry) (nullable): potentially empty or
   *     %NULL array of removed entries (a %NULL array is equivalent to an empty one)
   *
   * Emitted when the set of schedule entries known to the scheduler changes. It
   * is emitted at the same time as #GObject::notify for the
   * #MwsScheduler:entries property, but contains the delta of which
   * entries have been added and removed.
   *
   * There will be at least one entry in one of the arrays.
   *
   * Since: 0.1.0
   */
  g_signal_new ("entries-changed", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                G_TYPE_NONE, 2, G_TYPE_PTR_ARRAY, G_TYPE_PTR_ARRAY);

  /**
   * MwsScheduler::active-entries-changed:
   * @self: a #MwsScheduler
   * @added: (element-type MwsScheduleEntry) (nullable): potentially empty or
   *     %NULL array of newly-active entries (a %NULL array is equivalent to an
   *     empty one)
   * @removed: (element-type MwsScheduleEntry) (nullable): potentially empty or
   *     %NULL array of newly-inactive entries (a %NULL array is equivalent to
   *     an empty one)
   *
   * Emitted when the set of active entries changes; i.e. when an entry is
   * allowed to start downloading, or when one is requested to stop downloading.
   *
   * There will be at least one entry in one of the arrays.
   *
   * Since: 0.1.0
   */
  g_signal_new ("active-entries-changed", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                G_TYPE_NONE, 2, G_TYPE_PTR_ARRAY, G_TYPE_PTR_ARRAY);
}

static void
mws_scheduler_init (MwsScheduler *self)
{
  self->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         NULL, g_object_unref);
  self->max_entries = DEFAULT_MAX_ENTRIES;
  self->entries_data = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              NULL, (GDestroyNotify) entry_data_free);
}

static void
mws_scheduler_dispose (GObject *object)
{
  MwsScheduler *self = MWS_SCHEDULER (object);

  g_clear_pointer (&self->entries, g_hash_table_unref);
  g_clear_pointer (&self->entries_data, g_hash_table_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_scheduler_parent_class)->dispose (object);
}

static void
mws_scheduler_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MwsScheduler *self = MWS_SCHEDULER (object);

  switch ((MwsSchedulerProperty) property_id)
    {
    case PROP_ENTRIES:
      g_value_set_boxed (value, self->entries);
      break;
    case PROP_MAX_ENTRIES:
      g_value_set_uint (value, self->max_entries);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_scheduler_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MwsScheduler *self = MWS_SCHEDULER (object);

  switch ((MwsSchedulerProperty) property_id)
    {
    case PROP_ENTRIES:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_MAX_ENTRIES:
      /* Construct only. */
      g_assert (self->max_entries == 0);
      self->max_entries = g_value_get_uint (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * mws_scheduler_new:
 *
 * Create a new #MwsScheduler instance, with no schedule entries to begin
 * with.
 *
 * Returns: (transfer full): a new #MwsScheduler
 * Since: 0.1.0
 */
MwsScheduler *
mws_scheduler_new (void)
{
  return g_object_new (MWS_TYPE_SCHEDULER, NULL);
}

/**
 * mws_scheduler_update_entries:
 * @self: a #MwsScheduler
 * @added: (nullable) (transfer none) (element-type MwsScheduleEntry): set of
 *    #MwsScheduleEntry instances to add to the scheduler
 * @removed: (nullable) (transfer none) (element-type utf8): set of entry IDs
 *    to remove from the scheduler
 * @error: return location for a #GError, or %NULL
 *
 * Update the set of schedule entries in the scheduler, adding all entries in
 * @added, and removing all those in @removed.
 *
 * Entries in @added which are already in the scheduler are duplicated; entries
 * in @removed which are not in the scheduler are ignored.
 *
 * If adding any of @added to the scheduler would cause it to exceed
 * #MwsScheduler:max-entries, %MWS_SCHEDULER_ERROR_FULL will be returned and
 * the scheduler will not be modified to add or remove any of @added or
 * @removed.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_scheduler_update_entries (MwsScheduler  *self,
                              GPtrArray     *added,
                              GPtrArray     *removed,
                              GError       **error)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_autoptr (GPtrArray) actually_added = NULL;  /* (element-type MwsScheduleEntry) */
  actually_added = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr (GPtrArray) actually_removed = NULL;  /* (element-type MwsScheduleEntry) */
  actually_removed = g_ptr_array_new_with_free_func (g_object_unref);

  /* Check resource limits. */
  if (added != NULL &&
      added->len > self->max_entries - g_hash_table_size (self->entries))
    {
      g_set_error (error, MWS_SCHEDULER_ERROR, MWS_SCHEDULER_ERROR_FULL,
                   _("Too many ongoing downloads already."));
      return FALSE;
    }

  for (gsize i = 0; removed != NULL && i < removed->len; i++)
    {
      const gchar *entry_id = removed->pdata[i];
      g_return_val_if_fail (mws_schedule_entry_id_is_valid (entry_id), FALSE);

      g_debug ("Removing schedule entry ‘%s’.", entry_id);

      /* FIXME: Upstream a g_hash_table_steal_extended() function which combines these two. */
      gpointer value;
      if (g_hash_table_lookup_extended (self->entries, entry_id, NULL, &value))
        {
          g_autoptr(MwsScheduleEntry) entry = value;
          g_hash_table_steal (self->entries, entry_id);
          g_assert (g_hash_table_remove (self->entries_data, entry_id));
          g_ptr_array_add (actually_removed, g_steal_pointer (&entry));
        }
      else
        {
          g_debug ("Schedule entry ‘%s’ did not exist in MwsScheduler %p.",
                   entry_id, self);
          g_assert (g_hash_table_lookup (self->entries_data, entry_id) == NULL);
        }
    }

  for (gsize i = 0; added != NULL && i < added->len; i++)
    {
      MwsScheduleEntry *entry = added->pdata[i];
      const gchar *entry_id = mws_schedule_entry_get_id (entry);
      g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (entry), FALSE);

      g_debug ("Adding schedule entry ‘%s’.", entry_id);

      if (g_hash_table_replace (self->entries,
                                (gpointer) entry_id, g_object_ref (entry)))
        {
          g_hash_table_replace (self->entries_data,
                                (gpointer) entry_id, entry_data_new ());
          g_ptr_array_add (actually_added, g_object_ref (entry));
        }
      else
        {
          g_debug ("Schedule entry ‘%s’ already existed in MwsScheduler %p.",
                   entry_id, self);
          g_assert (g_hash_table_lookup (self->entries_data, entry_id) != NULL);
        }
    }

  if (actually_added->len > 0 || actually_removed->len > 0)
    {
      g_debug ("%s: Emitting entries-changed with %u added, %u removed",
               G_STRFUNC, actually_added->len, actually_removed->len);
      g_object_notify (G_OBJECT (self), "entries");
      g_signal_emit_by_name (G_OBJECT (self), "entries-changed",
                             actually_added, actually_removed);

      /* Trigger a reschedule due to the new entries. */
      mws_scheduler_reschedule (self);
    }

  return TRUE;
}

/**
 * mws_scheduler_remove_entries_for_owner:
 * @self: a #MwsScheduler
 * @owner: the D-Bus unique name of the peer to remove entries for
 * @error: return location for a #GError, or %NULL
 *
 * Remove all schedule entries from the #MwsScheduler whose owner is @owner.
 * Possible errors are the same as for mws_scheduler_update_entries().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_scheduler_remove_entries_for_owner (MwsScheduler  *self,
                                        const gchar   *owner,
                                        GError       **error)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), FALSE);
  g_return_val_if_fail (g_dbus_is_unique_name (owner), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_autoptr(GPtrArray) entries_to_remove = g_ptr_array_new_with_free_func (NULL);

  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, self->entries);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (value);

      if (g_str_equal (mws_schedule_entry_get_owner (entry), owner))
        g_ptr_array_add (entries_to_remove,
                         (gpointer) mws_schedule_entry_get_id (entry));
    }

  return mws_scheduler_update_entries (self, NULL, entries_to_remove, error);
}

/**
 * mws_scheduler_get_entry:
 * @self: a #MwsScheduler
 * @entry_id: ID of the schedule entry to look up
 *
 * Look up the given @entry_id in the scheduler and return it if found;
 * otherwise return %NULL.
 *
 * Returns: (transfer none) (nullable): the found entry, or %NULL
 * Since: 0.1.0
 */
MwsScheduleEntry *
mws_scheduler_get_entry (MwsScheduler *self,
                         const gchar  *entry_id)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), NULL);
  g_return_val_if_fail (mws_schedule_entry_id_is_valid (entry_id), NULL);

  return g_hash_table_lookup (self->entries, entry_id);
}

/**
 * mws_scheduler_get_entries:
 * @self: a #MwsScheduler
 *
 * Get the complete set of schedule entries known to the scheduler, as a map of
 * #MwsScheduleEntry instances indexed by entry ID.
 *
 * Returns: (transfer none) (element-type utf8 MwsScheduleEntry): mapping of
 *    entry IDs to entries
 * Since: 0.1.0
 */
GHashTable *
mws_scheduler_get_entries (MwsScheduler *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), NULL);

  return self->entries;
}

/**
 * mws_scheduler_is_entry_active:
 * @self: a #MwsScheduler
 * @entry: the entry
 *
 * Checks whether the given entry is currently allowed to be downloaded. This
 * only checks cached state: it does not recalculate the scheduler state.
 *
 * It is an error to call this on an @entry which is not currently in the
 * scheduler.
 *
 * Returns: %TRUE if entry can be downloaded now, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_scheduler_is_entry_active (MwsScheduler     *self,
                               MwsScheduleEntry *entry)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), FALSE);
  g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (entry), FALSE);

  const gchar *entry_id = mws_schedule_entry_get_id (entry);
  const EntryData *data = g_hash_table_lookup (self->entries_data, entry_id);
  g_return_val_if_fail (data != NULL, FALSE);

  g_debug ("%s: Entry ‘%s’, active: %s",
           G_STRFUNC, entry_id, data->is_active ? "yes" : "no");

  return data->is_active;
}

/**
 * mws_scheduler_reschedule:
 * @self: a #MwsScheduler
 *
 * Calculate an updated download schedule for all currently active entries, and
 * update the set of active entries if necessary. Changes to the set of active
 * entries will be signalled using #MwsScheduler::active-entries-changed.
 *
 * This is called automatically when the set of entries in the scheduler
 * changes, or when any relevant input to the scheduler changes; so should not
 * normally need to be called manually. It is exposed mainly for unit testing.
 *
 * Since: 0.1.0
 */
void
mws_scheduler_reschedule (MwsScheduler *self)
{
  g_return_if_fail (MWS_IS_SCHEDULER (self));

  g_debug ("%s: Rescheduling %u entries",
           G_STRFUNC, g_hash_table_size (self->entries));

  /* Sanity checks. */
  g_assert (g_hash_table_size (self->entries) ==
            g_hash_table_size (self->entries_data));

  /* Fast path. */
  if (g_hash_table_size (self->entries) == 0)
    return;

  /* Can we schedule everything for download? */
  /* FIXME: Abstract the network monitor to query NetworkManager directly. */
  GNetworkMonitor *monitor = g_network_monitor_get_default ();
  gboolean active = !g_network_monitor_get_network_metered (monitor);

  g_debug ("%s: Active: %d (using network monitor: %s)",
           G_STRFUNC, active, g_type_name (G_OBJECT_TYPE (monitor)));

  /* For each entry, see if it’s permissible to start downloading it. For the
   * moment, we only use whether the network is metered as a basis for this
   * calculation. In future, we can factor in the tariff on each connection,
   * bandwidth usage, capacity limits, etc. */
  g_autoptr(GPtrArray) entries_now_active = g_ptr_array_new_with_free_func (NULL);
  g_autoptr(GPtrArray) entries_were_active = g_ptr_array_new_with_free_func (NULL);

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init (&iter, self->entries);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *entry_id = key;
      MwsScheduleEntry *entry = value;
      EntryData *data = g_hash_table_lookup (self->entries_data, entry_id);
      g_assert (data != NULL);

      /* Accounting for the signal emission at the end of the function. */
      if (data->is_active && !active)
        g_ptr_array_add (entries_were_active, entry);
      else if (!data->is_active && active)
        g_ptr_array_add (entries_now_active, entry);

      /* Update this entry’s status. */
      data->is_active = active;
    }

  /* Signal the changes. */
  if (entries_now_active->len > 0 || entries_were_active->len > 0)
    {
      g_debug ("%s: Emitting active-entries-changed with %u now active, %u no longer active",
               G_STRFUNC, entries_now_active->len, entries_were_active->len);
      g_signal_emit_by_name (G_OBJECT (self), "active-entries-changed",
                             entries_now_active, entries_were_active);
    }
}
