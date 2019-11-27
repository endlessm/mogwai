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
#include <libmogwai-schedule/clock.h>
#include <libmogwai-schedule/connection-monitor.h>
#include <libmogwai-schedule/peer-manager.h>
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

static void mws_scheduler_constructed  (GObject      *object);
static void mws_scheduler_dispose      (GObject      *object);

static void mws_scheduler_get_property (GObject      *object,
                                        guint         property_id,
                                        GValue        *value,
                                        GParamSpec   *pspec);
static void mws_scheduler_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec);

static void connection_monitor_connections_changed_cb        (MwsConnectionMonitor *connection_monitor,
                                                              GPtrArray            *added,
                                                              GPtrArray            *removed,
                                                              gpointer              user_data);
static void connection_monitor_connection_details_changed_cb (MwsConnectionMonitor *connection_monitor,
                                                              const gchar          *connection_id,
                                                              gpointer              user_data);
static void peer_manager_peer_vanished_cb                    (MwsPeerManager       *manager,
                                                              const gchar          *name,
                                                              gpointer              user_data);
static void clock_offset_changed_cb                          (MwsClock             *clock,
                                                              gpointer              user_data);

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

  /* Scheduling data sources. */
  MwsConnectionMonitor *connection_monitor;  /* (owned) */
  MwsPeerManager *peer_manager;  /* (owned) */
  MwsClock *clock;  /* (owned) */

  /* Time tracking. */
  guint reschedule_alarm_id;  /* 0 when no reschedule is scheduled */

  /* Mapping from entry ID to (not nullable) entry. */
  GHashTable *entries;  /* (owned) (element-type utf8 MwsScheduleEntry) */
  gsize max_entries;

  /* Mapping from entry ID to (not nullable) entry data. We can’t use the same
   * hash table as @entries since we need to be able to return that one in
   * mws_scheduler_get_entries(). Always has the same set of keys as @entries. */
  GHashTable *entries_data;  /* (owned) (element-type utf8 EntryData) */

  /* Maximum number of downloads allowed to be active at the same time. */
  guint max_active_entries;

  /* Cache of some of the connection data used by our properties. */
  gboolean cached_allow_downloads;

  /* Sanity check that we don’t reschedule re-entrantly. */
  gboolean in_reschedule;
};

/* Arbitrarily chosen. */
static const gsize DEFAULT_MAX_ENTRIES = 1024;
/* Chosen for a few reasons:
 *  1. OSTree app updates and installs take ungodly amounts of I/O and CPU —
 *     doing more than one of these at a time in the background is an
 *     aggressively bad UX
 *  2. Over-parallelisation causes bandwidth hogging and reduces the amount of
 *     bandwidth available for foreground applications or user interactivity
 *  3. We don’t want head-of-line blocking by large OS updates to block smaller,
 *     more-regular content updates.
 */
static const guint DEFAULT_MAX_ACTIVE_ENTRIES = 1;

typedef enum
{
  PROP_ENTRIES = 1,
  PROP_MAX_ENTRIES,
  PROP_CONNECTION_MONITOR,
  PROP_MAX_ACTIVE_ENTRIES,
  PROP_PEER_MANAGER,
  PROP_ALLOW_DOWNLOADS,
  PROP_CLOCK,
} MwsSchedulerProperty;

G_DEFINE_TYPE (MwsScheduler, mws_scheduler, G_TYPE_OBJECT)

static void
mws_scheduler_class_init (MwsSchedulerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_CLOCK + 1] = { NULL, };

  object_class->constructed = mws_scheduler_constructed;
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
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:connection-monitor:
   *
   * A #MwsConnectionMonitor instance to provide information about the currently
   * active network connections which is relevant to scheduling downloads, such
   * as whether they are currently metered, how much of their capacity has been
   * used in the current time period, or any user-provided policies for them.
   *
   * Since: 0.1.0
   */
  props[PROP_CONNECTION_MONITOR] =
      g_param_spec_object ("connection-monitor", "Connection Monitor",
                           "A #MwsConnectionMonitor instance to provide "
                           "information about the currently active network connections.",
                           MWS_TYPE_CONNECTION_MONITOR,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:max-active-entries:
   *
   * Maximum number of schedule entries which can be active at any time. This
   * effectively limits the parallelisation of the scheduler. In contrast with
   * #MwsScheduler:max-entries, this limit is expected to be reached routinely.
   *
   * Since: 0.1.0
   */
  props[PROP_MAX_ACTIVE_ENTRIES] =
      g_param_spec_uint ("max-active-entries", "Max. Active Entries",
                         "Maximum number of schedule entries which can be active at any time.",
                         1, G_MAXUINT, DEFAULT_MAX_ACTIVE_ENTRIES,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:peer-manager:
   *
   * A #MwsPeerManager instance to provide information about the peers who are
   * adding schedule entries to the scheduler. (Typically, these are D-Bus peers
   * using the scheduler’s D-Bus interface.)
   *
   * Since: 0.1.0
   */
  props[PROP_PEER_MANAGER] =
      g_param_spec_object ("peer-manager", "Peer Manager",
                           "A #MwsPeerManager instance to provide information "
                           "about the peers who are adding schedule entries to "
                           "the scheduler.",
                           MWS_TYPE_PEER_MANAGER,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:allow-downloads:
   *
   * Whether any of the currently active network connections are configured to
   * allow any large downloads. It is up to the clients which use Mogwai to
   * decide what ’large’ is.
   *
   * This is not a guarantee that a schedule entry
   * will be scheduled; it is a reflection of the user’s intent for the use of
   * the currently active network connections, intended to be used in UIs to
   * remind the user of how they have configured the network.
   *
   * Programs must not use this value to check whether to schedule an entry.
   * Schedule the entry unconditionally; the scheduler will work out whether
   * (and when) to download the entry.
   *
   * Since: 0.1.0
   */
  props[PROP_ALLOW_DOWNLOADS] =
      g_param_spec_boolean ("allow-downloads", "Allow Downloads",
                            "Whether any of the currently active network "
                            "connections are configured to allow any large "
                            "downloads.",
                            TRUE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduler:clock:
   *
   * A #MwsClock instance to provide wall clock timing and alarms for time-based
   * scheduling. Typically, this is provided by a #MwsClockSystem, using the
   * system clock.
   *
   * Since: 0.1.0
   */
  props[PROP_CLOCK] =
      g_param_spec_object ("clock", "Clock",
                           "A #MwsClock instance to provide wall clock timing "
                           "and alarms for time-based scheduling.",
                           MWS_TYPE_CLOCK,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

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
  self->max_active_entries = DEFAULT_MAX_ACTIVE_ENTRIES;
}

static void
mws_scheduler_constructed (GObject *object)
{
  MwsScheduler *self = MWS_SCHEDULER (object);

  G_OBJECT_CLASS (mws_scheduler_parent_class)->constructed (object);

  /* Check we have our construction properties. */
  g_assert (MWS_IS_CONNECTION_MONITOR (self->connection_monitor));
  g_assert (MWS_IS_PEER_MANAGER (self->peer_manager));
  g_assert (MWS_IS_CLOCK (self->clock));

  /* Connect to signals from the connection monitor, which will trigger
   * rescheduling. */
  g_signal_connect (self->connection_monitor, "connections-changed",
                    (GCallback) connection_monitor_connections_changed_cb, self);
  g_signal_connect (self->connection_monitor, "connection-details-changed",
                    (GCallback) connection_monitor_connection_details_changed_cb, self);

  /* Connect to signals from the peer manager, which will trigger removal of
   * entries when a peer disappears. */
  g_signal_connect (self->peer_manager, "peer-vanished",
                    (GCallback) peer_manager_peer_vanished_cb, self);

  /* Connect to signals from the clock, which will trigger rescheduling when
   * the clock offset changes. */
  g_signal_connect (self->clock, "offset-changed",
                    (GCallback) clock_offset_changed_cb, self);

  /* Initialise self->cached_allow_downloads. */
  mws_scheduler_reschedule (self);
}

static void
mws_scheduler_dispose (GObject *object)
{
  MwsScheduler *self = MWS_SCHEDULER (object);

  g_clear_pointer (&self->entries, g_hash_table_unref);
  g_clear_pointer (&self->entries_data, g_hash_table_unref);

  if (self->connection_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->connection_monitor,
                                            connection_monitor_connections_changed_cb,
                                            self);
      g_signal_handlers_disconnect_by_func (self->connection_monitor,
                                            connection_monitor_connection_details_changed_cb,
                                            self);
    }

  g_clear_object (&self->connection_monitor);

  if (self->peer_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->peer_manager,
                                            peer_manager_peer_vanished_cb,
                                            self);
    }

  g_clear_object (&self->peer_manager);

  if (self->clock != NULL && self->reschedule_alarm_id != 0)
    {
      mws_clock_remove_alarm (self->clock, self->reschedule_alarm_id);
      self->reschedule_alarm_id = 0;
    }

  if (self->clock != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->clock,
                                            clock_offset_changed_cb,
                                            self);
    }

  g_clear_object (&self->clock);

  g_assert (!self->in_reschedule);

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
    case PROP_CONNECTION_MONITOR:
      g_value_set_object (value, self->connection_monitor);
      break;
    case PROP_MAX_ACTIVE_ENTRIES:
      g_value_set_uint (value, self->max_active_entries);
      break;
    case PROP_PEER_MANAGER:
      g_value_set_object (value, self->peer_manager);
      break;
    case PROP_ALLOW_DOWNLOADS:
      g_value_set_boolean (value, mws_scheduler_get_allow_downloads (self));
      break;
    case PROP_CLOCK:
      g_value_set_object (value, self->clock);
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
    case PROP_ALLOW_DOWNLOADS:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_MAX_ENTRIES:
      /* Construct only. */
      self->max_entries = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_MONITOR:
      /* Construct only. */
      g_assert (self->connection_monitor == NULL);
      self->connection_monitor = g_value_dup_object (value);
      break;
    case PROP_MAX_ACTIVE_ENTRIES:
      /* Construct only. */
      self->max_active_entries = g_value_get_uint (value);
      break;
    case PROP_PEER_MANAGER:
      /* Construct only. */
      g_assert (self->peer_manager == NULL);
      self->peer_manager = g_value_dup_object (value);
      break;
    case PROP_CLOCK:
      /* Construct only. */
      g_assert (self->clock == NULL);
      self->clock = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
connection_monitor_connections_changed_cb (MwsConnectionMonitor *connection_monitor,
                                           GPtrArray            *added,
                                           GPtrArray            *removed,
                                           gpointer              user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);

  /* This needs to update self->cached_allow_downloads too. */
  g_debug ("%s: Connections changed (%u added, %u removed)",
           G_STRFUNC, (added != NULL) ? added->len : 0,
           (removed != NULL) ? removed->len : 0);
  mws_scheduler_reschedule (self);
}

static void
connection_monitor_connection_details_changed_cb (MwsConnectionMonitor *connection_monitor,
                                                  const gchar          *connection_id,
                                                  gpointer              user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);

  /* This needs to update self->cached_allow_downloads too. */
  g_debug ("%s: Connection ‘%s’ changed details", G_STRFUNC, connection_id);
  mws_scheduler_reschedule (self);
}

static void
peer_manager_peer_vanished_cb (MwsPeerManager *manager,
                               const gchar    *name,
                               gpointer        user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);
  g_autoptr(GError) local_error = NULL;

  /* Remove the schedule entries for this peer. */
  if (!mws_scheduler_remove_entries_for_owner (self, name, &local_error))
    {
      g_debug ("Failed to remove schedule entries for owner ‘%s’: %s",
               name, local_error->message);
    }
}

static void
clock_offset_changed_cb (MwsClock *clock,
                         gpointer  user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);

  g_autoptr(GDateTime) now = mws_clock_get_now_local (clock);
  g_autofree gchar *now_str = g_date_time_format (now, "%FT%T%:::z");

  g_debug ("%s: Clock offset changed; time is now %s", G_STRFUNC, now_str);
  mws_scheduler_reschedule (self);
}

/**
 * mws_scheduler_new:
 * @connection_monitor: (transfer none): a #MwsConnectionMonitor to provide
 *    information about network connections to the scheduler
 * @peer_manager: (transfer none): a #MwsPeerManager to provide information
 *    about peers which are adding schedule entries to the scheduler
 * @clock: (transfer none): a #MwsClock to provide timing information
 *
 * Create a new #MwsScheduler instance, with no schedule entries to begin
 * with.
 *
 * Returns: (transfer full): a new #MwsScheduler
 * Since: 0.1.0
 */
MwsScheduler *
mws_scheduler_new (MwsConnectionMonitor *connection_monitor,
                   MwsPeerManager       *peer_manager,
                   MwsClock             *clock)
{
  g_return_val_if_fail (MWS_IS_CONNECTION_MONITOR (connection_monitor), NULL);
  g_return_val_if_fail (MWS_IS_PEER_MANAGER (peer_manager), NULL);
  g_return_val_if_fail (MWS_IS_CLOCK (clock), NULL);

  return g_object_new (MWS_TYPE_SCHEDULER,
                       "connection-monitor", connection_monitor,
                       "peer-manager", peer_manager,
                       "clock", clock,
                       NULL);
}

/**
 * mws_scheduler_get_peer_manager:
 * @self: a #MwsScheduler
 *
 * Get the value of #MwsScheduler:peer-manager.
 *
 * Returns: (transfer none): the peer manager for the scheduler
 * Since: 0.1.0
 */
MwsPeerManager *
mws_scheduler_get_peer_manager (MwsScheduler *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), NULL);

  return self->peer_manager;
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
 * Entries in @added which are already in the scheduler, and entries in @removed
 * which are not in the scheduler, are ignored.
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

  g_autoptr(GPtrArray) actually_added = NULL;  /* (element-type MwsScheduleEntry) */
  actually_added = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr(GPtrArray) actually_removed = NULL;  /* (element-type MwsScheduleEntry) */
  actually_removed = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr(GPtrArray) actually_removed_active = NULL;  /* (element-type MwsScheduleEntry) */
  actually_removed_active = g_ptr_array_new_with_free_func (g_object_unref);

  /* Check resource limits. */
  if (added != NULL &&
      added->len > self->max_entries - g_hash_table_size (self->entries))
    {
      g_set_error (error, MWS_SCHEDULER_ERROR, MWS_SCHEDULER_ERROR_FULL,
                   _("Too many ongoing downloads already."));
      return FALSE;
    }

  /* Remove and add entries. Throughout, we need to ensure that @entries and
   * @entries_data always have identical sets of keys; that reduces the number
   * of checks needed in other places in the code. */
  for (gsize i = 0; removed != NULL && i < removed->len; i++)
    {
      const gchar *entry_id = removed->pdata[i];
      g_return_val_if_fail (mws_schedule_entry_id_is_valid (entry_id), FALSE);

      g_debug ("Removing schedule entry ‘%s’.", entry_id);

      /* FIXME: Upstream a g_hash_table_steal_extended() function which combines these two.
       * See: https://bugzilla.gnome.org/show_bug.cgi?id=795302 */
      gpointer value;
      if (g_hash_table_lookup_extended (self->entries, entry_id, NULL, &value))
        {
          gboolean was_active = mws_scheduler_is_entry_active (self, MWS_SCHEDULE_ENTRY (value));

          g_autoptr(MwsScheduleEntry) entry = value;
          g_hash_table_steal (self->entries, entry_id);
          g_assert (g_hash_table_remove (self->entries_data, entry_id));

          if (was_active)
            g_ptr_array_add (actually_removed_active, g_object_ref (entry));
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

  if (actually_removed_active->len > 0)
    {
      g_debug ("%s: Emitting active-entries-changed with %u added, %u removed",
               G_STRFUNC, (guint) 0, actually_removed_active->len);
      g_signal_emit_by_name (G_OBJECT (self), "active-entries-changed",
                             NULL, actually_removed_active);
    }

  if (actually_added->len > 0 || actually_removed->len > 0)
    {
      g_debug ("%s: Emitting entries-changed with %u added, %u removed",
               G_STRFUNC, actually_added->len, actually_removed->len);
      g_object_notify (G_OBJECT (self), "entries");
      g_signal_emit_by_name (G_OBJECT (self), "entries-changed",
                             actually_added, actually_removed);

      /* Trigger a reschedule due to the new or removed entries. */
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

static gboolean
reschedule_cb (gpointer user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);
  mws_scheduler_reschedule (self);
  return G_SOURCE_REMOVE;
}

/* Get the priority of a given peer. Higher returned numbers indicate more
 * important peers. */
static gint
get_peer_priority (MwsScheduler     *self,
                   MwsScheduleEntry *entry)
{
  const gchar *owner = mws_schedule_entry_get_owner (entry);
  const gchar *owner_path =
      mws_peer_manager_get_peer_credentials (self->peer_manager, owner);

  /* If we haven’t got credentials for this peer (which would be unexpected and
   * indicate a serious problem), give it a low priority. */
  if (owner_path == NULL)
    return G_MININT;

  /* The OS and app updaters are equally as important as each other. The actual
   * priority numbers chosen here are fairly arbitrary; it’s the partial order
   * over them which is important. */
  if (g_str_equal (owner_path, "/usr/libexec/eos-updater") ||
      g_str_equal (owner_path, "/usr/bin/gnome-software"))
    return G_MAXINT;

  /* Anything else goes in the range (G_MININT, G_MAXINT). */
  gint priority = g_str_hash (owner_path) + G_MININT;
  if (priority == G_MININT)
    priority += 1;
  if (priority == G_MAXINT)
    priority -= 1;
  return priority;
}

/* Compare entries to give a total order by scheduling priority, with the most
 * important entries for scheduling listed first. i.e. Given entries @a and @b,
 * this will return a negative number if @a should be scheduled before @b.
 * This is one of the core parts of the scheduling algorithm; earlier stages
 * trim any entries which can’t be scheduled (for example, due to wanting to use
 * a metered connection when the user has disallowed it); later stages select
 * the most important N entries to actually schedule, according to
 * parallelisation limits. */
static gint
entry_compare_cb (gconstpointer a_,
                  gconstpointer b_,
                  gpointer      user_data)
{
  MwsScheduler *self = MWS_SCHEDULER (user_data);
  MwsScheduleEntry *a = MWS_SCHEDULE_ENTRY (*((MwsScheduleEntry **) a_));
  MwsScheduleEntry *b = MWS_SCHEDULE_ENTRY (*((MwsScheduleEntry **) b_));

  /* As per https://phabricator.endlessm.com/T21327, we want the following
   * priority order (most important first):
   *  1. App extensions to com.endlessm.* apps
   *  2. OS updates
   *  3. App updates and app extensions to non-Endless apps
   *  4. Anything else
   *
   * We currently have two inputs to base this on: the identity of the peer who
   * owns a #MwsScheduleEntry, and the priority they set for the entry.
   * Priorities are scoped to an owner, not global.
   *
   * We can implement what we want by hard-coding it so that the scopes for
   * gnome-software and eos-updater are merged, and both given priority over any
   * other peer. Then getting the ordering we want is a matter of coordinating
   * the priorities set by gnome-software and eos-updater on their schedule
   * entries, which is possible since we control all of that code. */

  /* Sort by peer first. */
  gint a_peer_priority = get_peer_priority (self, a);
  gint b_peer_priority = get_peer_priority (self, b);

  if (a_peer_priority != b_peer_priority)
    {
      g_debug ("%s: Comparing schedule entries ‘%s’ and ‘%s’ by peer priority: %d vs %d",
               G_STRFUNC, mws_schedule_entry_get_id (a),
               mws_schedule_entry_get_id (b), a_peer_priority, b_peer_priority);
      return (b_peer_priority > a_peer_priority) ? 1 : -1;
    }

  /* Within the peer, sort by the priority assigned by that peer to the entry. */
  guint32 a_entry_priority = mws_schedule_entry_get_priority (a);
  guint32 b_entry_priority = mws_schedule_entry_get_priority (b);

  if (a_entry_priority != b_entry_priority)
    {
      g_debug ("%s: Comparing schedule entries ‘%s’ and ‘%s’ by entry priority: %u vs %u",
               G_STRFUNC, mws_schedule_entry_get_id (a),
               mws_schedule_entry_get_id (b), a_entry_priority,
               b_entry_priority);
      return (b_entry_priority > a_entry_priority) ? 1 : -1;
    }

  /* Arbitrarily break ties using the entries’ IDs, which should always be
   * different. */
  g_debug ("%s: Comparing schedule entries ‘%s’ and ‘%s’ by entry ID",
           G_STRFUNC, mws_schedule_entry_get_id (a),
           mws_schedule_entry_get_id (b));
  return g_strcmp0 (mws_schedule_entry_get_id (a),
                    mws_schedule_entry_get_id (b));
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

  g_assert (!self->in_reschedule);
  self->in_reschedule = TRUE;

  g_debug ("%s: Rescheduling %u entries",
           G_STRFUNC, g_hash_table_size (self->entries));

  /* Sanity checks. */
  g_assert (g_hash_table_size (self->entries) ==
            g_hash_table_size (self->entries_data));

  /* Clear any pending reschedule. */
  if (self->reschedule_alarm_id != 0)
    {
      mws_clock_remove_alarm (self->clock, self->reschedule_alarm_id);
      self->reschedule_alarm_id = 0;
    }

  /* Preload information from the connection monitor. */
  const gchar * const *all_connection_ids = NULL;
  all_connection_ids = mws_connection_monitor_get_connection_ids (self->connection_monitor);
  gsize n_connections = g_strv_length ((gchar **) all_connection_ids);

  g_autoptr(GArray) all_connection_details = g_array_sized_new (FALSE, FALSE,
                                                                sizeof (MwsConnectionDetails),
                                                                n_connections);
  g_array_set_clear_func (all_connection_details,
                          (GDestroyNotify) mws_connection_details_clear);
  g_array_set_size (all_connection_details, n_connections);

  gboolean cached_allow_downloads = TRUE;

  for (gsize i = 0; all_connection_ids[i] != NULL; i++)
    {
      MwsConnectionDetails *out_details = &g_array_index (all_connection_details,
                                                          MwsConnectionDetails, i);

      if (!mws_connection_monitor_get_connection_details (self->connection_monitor,
                                                          all_connection_ids[i],
                                                          out_details))
        {
          /* Fill the details with dummy values. */
          g_debug ("%s: Failed to get details for connection ‘%s’.",
                   G_STRFUNC, all_connection_ids[i]);
          mws_connection_details_clear (out_details);
          continue;
        }

      /* FIXME: See FIXME below by `can_be_active` about allowing clients to
       * specify whether they support downloading from selective connections.
       * If that logic changes, so does this. */
      cached_allow_downloads = cached_allow_downloads && out_details->allow_downloads;
    }

  if (self->cached_allow_downloads != cached_allow_downloads)
    {
      g_debug ("%s: Updating cached_allow_downloads from %u to %u",
               G_STRFUNC, (guint) self->cached_allow_downloads,
               (guint) cached_allow_downloads);
      self->cached_allow_downloads = cached_allow_downloads;
      g_object_notify (G_OBJECT (self), "allow-downloads");
    }

  /* Fast path. We still have to load the connection monitor information above,
   * though, so that we can update self->cached_allow_downloads. */
  if (g_hash_table_size (self->entries) == 0)
    {
      self->in_reschedule = FALSE;
      return;
    }

  /* As we iterate over all the entries, see when the earliest time we next need
   * to reschedule is. */
  g_autoptr(GDateTime) now = mws_clock_get_now_local (self->clock);
  g_autoptr(GDateTime) next_reschedule = NULL;

  g_autofree gchar *now_str = g_date_time_format (now, "%FT%T%:::z");
  g_debug ("%s: Considering now = %s", G_STRFUNC, now_str);

  /* For each entry, see if it’s permissible to start downloading it. For the
   * moment, we only use whether the network is metered as a basis for this
   * calculation. In future, we can factor in the tariff on each connection,
   * bandwidth usage, capacity limits, etc. */
  g_autoptr(GPtrArray) entries_now_active = g_ptr_array_new_with_free_func (NULL);
  g_autoptr(GPtrArray) entries_were_active = g_ptr_array_new_with_free_func (NULL);

  /* An array of the entries which can be active within the user’s preferences
   * for cost. Not necessarily all of them will be chosen to be active, though,
   * based on parallelisation limits and prioritisation. */
  g_autoptr(GPtrArray) entries_can_be_active = g_ptr_array_new_with_free_func (NULL);

  g_autoptr(GPtrArray) safe_connections = g_ptr_array_new_full (n_connections, NULL);

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init (&iter, self->entries);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *entry_id = key;
      MwsScheduleEntry *entry = value;
      EntryData *data = g_hash_table_lookup (self->entries_data, entry_id);
      g_assert (data != NULL);

      g_debug ("%s: Scheduling entry ‘%s’", G_STRFUNC, entry_id);

      /* Work out which connections this entry could be downloaded on safely. */
      g_ptr_array_set_size (safe_connections, 0);

      for (gsize i = 0; all_connection_ids[i] != NULL; i++)
        {
          /* FIXME: Support multi-path properly by allowing each
           * #MwsScheduleEntry to specify which connections it might download
           * over. Currently we assume the client might download over any
           * active connection. (Typically only one connection will ever be
           * active anyway.) */
          const MwsConnectionDetails *details = &g_array_index (all_connection_details,
                                                                MwsConnectionDetails, i);

          /* If this connection has a tariff specified, work out whether we’ve
           * hit any of the limits for the current tariff period. */
          MwtPeriod *tariff_period = NULL;
          gboolean tariff_period_reached_capacity_limit = FALSE;

          if (details->tariff != NULL)
            {
              tariff_period = mwt_tariff_lookup_period (details->tariff, now);
            }

          if (tariff_period != NULL)
            {
              g_autofree gchar *tariff_period_start_str =
                  g_date_time_format (mwt_period_get_start (tariff_period), "%FT%T%:::z");
              g_autofree gchar *tariff_period_end_str =
                  g_date_time_format (mwt_period_get_end (tariff_period), "%FT%T%:::z");
              g_debug ("%s: Considering tariff period %p: %s to %s",
                       G_STRFUNC, tariff_period, tariff_period_start_str,
                       tariff_period_end_str);
            }
          else
            {
              g_debug ("%s: No tariff period found", G_STRFUNC);
            }

          if (tariff_period != NULL)
            {
              /* FIXME: For the moment, we can only see if the capacity limit is
               * hard-coded to zero to indicate a period when downloads are
               * banned. In future, we will need to query the amount of data
               * downloaded in the current period and check it against the
               * limit (plus do a reschedule when the amount of data downloaded
               * does reach the limit). */
              tariff_period_reached_capacity_limit =
                  (mwt_period_get_capacity_limit (tariff_period) == 0);
            }

          /* Is it safe to schedule this entry on this connection now? */
          gboolean is_safe = ((details->metered == MWS_METERED_NO ||
                               details->metered == MWS_METERED_GUESS_NO ||
                               details->allow_downloads_when_metered) &&
                              details->allow_downloads &&
                              !tariff_period_reached_capacity_limit);
          g_debug ("%s: Connection ‘%s’ is %s to download entry ‘%s’ on "
                   "(metered: %s, allow-downloads-when-metered: %s, "
                   "allow-downloads: %s, tariff-period-reached-capacity-limit: %s).",
                   G_STRFUNC, all_connection_ids[i],
                   is_safe ? "safe" : "not safe", entry_id,
                   mws_metered_to_string (details->metered),
                   details->allow_downloads_when_metered ? "yes" : "no",
                   details->allow_downloads ? "yes" : "no",
                   tariff_period_reached_capacity_limit ? "yes" : "no");

          if (is_safe)
            g_ptr_array_add (safe_connections, (gpointer) all_connection_ids[i]);

          /* Work out when to do the next reschedule due to this tariff changing
           * periods. */
          if (details->tariff != NULL)
            {
              g_autoptr(GDateTime) next_transition = NULL;
              next_transition = mwt_tariff_get_next_transition (details->tariff, now,
                                                                NULL, NULL);

              g_autofree gchar *next_transition_str = NULL;
              if (next_transition != NULL)
                next_transition_str = g_date_time_format (next_transition, "%FT%T%:::z");
              else
                next_transition_str = g_strdup ("never");
              g_debug ("%s: Connection ‘%s’ next transition is %s",
                       G_STRFUNC, all_connection_ids[i], next_transition_str);

              if (next_transition != NULL &&
                  g_date_time_compare (now, next_transition) < 0 &&
                  (next_reschedule == NULL ||
                   g_date_time_compare (next_transition, next_reschedule) < 0))
                {
                  g_clear_pointer (&next_reschedule, g_date_time_unref);
                  next_reschedule = g_date_time_ref (next_transition);
                }
            }
        }

      /* If all the active connections are safe for this entry, it can be made
       * active. We assume that the client cannot support downloading over a
       * particular connection and ignoring another: all active connections have
       * to be safe to start a download.
       * FIXME: Allow clients to specify whether they support downloading from
       * selective connections. If so, their downloads could be made active
       * without all active connections having to be safe. */
      gboolean can_be_active = (safe_connections->len == n_connections);
      g_debug ("%s: Entry ‘%s’ %s (%u of %" G_GSIZE_FORMAT " connections are safe)",
               G_STRFUNC, entry_id,
               can_be_active ? "can be active" : "cannot be active",
               safe_connections->len, n_connections);

      if (can_be_active)
        {
          g_ptr_array_add (entries_can_be_active, entry);
        }
      else
        {
          /* Accounting for the signal emission at the end of the function. */
          if (data->is_active)
            g_ptr_array_add (entries_were_active, entry);

          /* Update this entry’s status. */
          data->is_active = FALSE;
        }
    }

  /* Order the potentially-active entries by priority. */
  g_ptr_array_sort_with_data (entries_can_be_active, entry_compare_cb, self);

  /* Take the most important N potentially-active entries and actually mark them
   * as active; mark the rest as not active. N is the maximum number of active
   * entries set at construction time for the scheduler. */
  for (gsize i = 0; i < entries_can_be_active->len; i++)
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (g_ptr_array_index (entries_can_be_active, i));
      const gchar *entry_id = mws_schedule_entry_get_id (entry);
      EntryData *data = g_hash_table_lookup (self->entries_data, entry_id);
      g_assert (data != NULL);

      gboolean active = (i < self->max_active_entries);
      g_debug ("%s: Entry ‘%s’ %s (index %" G_GSIZE_FORMAT " of %u sorted "
               "entries which can be active; limit of %u which will be active)",
               G_STRFUNC, entry_id,
               active ? "will be active" : "will not be active",
               i, entries_can_be_active->len, self->max_active_entries);

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

  /* Set up the next scheduling run. */
  if (next_reschedule != NULL)
    {
      /* FIXME: This doesn’t take into account the difference between monotonic
       * and wall clock time: if the computer suspends, or the timezone changes,
       * the reschedule will happen at the wrong time. We probably want to split
       * this out into a separate interface, with an implementation which can
       * monitor org.freedesktop.timedate1. */
      GTimeSpan interval = g_date_time_difference (next_reschedule, now);
      g_assert (interval >= 0);

      mws_clock_add_alarm (self->clock, next_reschedule, reschedule_cb, self, NULL);

      g_autofree gchar *next_reschedule_str = NULL;
      next_reschedule_str = g_date_time_format (next_reschedule, "%FT%T%:::z");
      g_debug ("%s: Setting next reschedule for %s (in %" G_GUINT64_FORMAT " seconds)",
               G_STRFUNC, next_reschedule_str, (guint64) interval / G_USEC_PER_SEC);
    }
  else
    {
      g_debug ("%s: Setting next reschedule to never", G_STRFUNC);
    }

  self->in_reschedule = FALSE;
}

/**
 * mws_scheduler_get_allow_downloads:
 * @self: a #MwsScheduler
 *
 * Get the value of #MwsScheduler:allow-downloads.
 *
 * Returns: %TRUE if the user has indicated that at least one of the active
 *    network connections should be used for large downloads, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_scheduler_get_allow_downloads (MwsScheduler *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULER (self), TRUE);

  return self->cached_allow_downloads;
}
