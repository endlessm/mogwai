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

#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/schedule-entry.h>
#include <libmogwai-schedule/schedule-entry-interface.h>
#include <libmogwai-schedule/schedule-service.h>
#include <libmogwai-schedule/scheduler.h>
#include <libmogwai-schedule/scheduler-interface.h>
#include <string.h>


static void mws_schedule_service_constructed  (GObject      *object);
static void mws_schedule_service_dispose      (GObject      *object);
static void mws_schedule_service_get_property (GObject      *object,
                                               guint         property_id,
                                               GValue       *value,
                                               GParamSpec   *pspec);
static void mws_schedule_service_set_property (GObject      *object,
                                               guint         property_id,
                                               const GValue *value,
                                               GParamSpec   *pspec);

static gchar **mws_schedule_service_entry_enumerate (GDBusConnection *connection,
                                                     const gchar     *sender,
                                                     const gchar     *object_path,
                                                     gpointer         user_data);
static GDBusInterfaceInfo **mws_schedule_service_entry_introspect (GDBusConnection *connection,
                                                                   const gchar     *sender,
                                                                   const gchar     *object_path,
                                                                   const gchar     *node,
                                                                   gpointer         user_data);
static const GDBusInterfaceVTable *mws_schedule_service_entry_dispatch (GDBusConnection *connection,
                                                                        const gchar     *sender,
                                                                        const gchar     *object_path,
                                                                        const gchar     *interface_name,
                                                                        const gchar     *node,
                                                                        gpointer        *out_user_data,
                                                                        gpointer         user_data);
static void mws_schedule_service_entry_method_call (GDBusConnection       *connection,
                                                    const gchar           *sender,
                                                    const gchar           *object_path,
                                                    const gchar           *interface_name,
                                                    const gchar           *method_name,
                                                    GVariant              *parameters,
                                                    GDBusMethodInvocation *invocation,
                                                    gpointer               user_data);

static void mws_schedule_service_entry_properties_get     (MwsScheduleService    *self,
                                                           MwsScheduleEntry      *entry,
                                                           GDBusConnection       *connection,
                                                           const gchar           *sender,
                                                           GVariant              *parameters,
                                                           GDBusMethodInvocation *invocation);
static void mws_schedule_service_entry_properties_set     (MwsScheduleService    *self,
                                                           MwsScheduleEntry      *entry,
                                                           GDBusConnection       *connection,
                                                           const gchar           *sender,
                                                           GVariant              *parameters,
                                                           GDBusMethodInvocation *invocation);
static void mws_schedule_service_entry_properties_get_all (MwsScheduleService    *self,
                                                           MwsScheduleEntry      *entry,
                                                           GDBusConnection       *connection,
                                                           const gchar           *sender,
                                                           GVariant              *parameters,
                                                           GDBusMethodInvocation *invocation);

static void mws_schedule_service_entry_remove                   (MwsScheduleService    *self,
                                                                 MwsScheduleEntry      *entry,
                                                                 GDBusConnection       *connection,
                                                                 const gchar           *sender,
                                                                 GVariant              *parameters,
                                                                 GDBusMethodInvocation *invocation);

static void mws_schedule_service_scheduler_method_call (GDBusConnection       *connection,
                                                        const gchar           *sender,
                                                        const gchar           *object_path,
                                                        const gchar           *interface_name,
                                                        const gchar           *method_name,
                                                        GVariant              *parameters,
                                                        GDBusMethodInvocation *invocation,
                                                        gpointer               user_data);

static void mws_schedule_service_scheduler_properties_get     (MwsScheduleService    *self,
                                                               GDBusConnection       *connection,
                                                               const gchar           *sender,
                                                               GVariant              *parameters,
                                                               GDBusMethodInvocation *invocation);
static void mws_schedule_service_scheduler_properties_set     (MwsScheduleService    *self,
                                                               GDBusConnection       *connection,
                                                               const gchar           *sender,
                                                               GVariant              *parameters,
                                                               GDBusMethodInvocation *invocation);
static void mws_schedule_service_scheduler_properties_get_all (MwsScheduleService    *self,
                                                               GDBusConnection       *connection,
                                                               const gchar           *sender,
                                                               GVariant              *parameters,
                                                               GDBusMethodInvocation *invocation);
static void mws_schedule_service_scheduler_schedule           (MwsScheduleService    *self,
                                                               GDBusConnection       *connection,
                                                               const gchar           *sender,
                                                               GVariant              *parameters,
                                                               GDBusMethodInvocation *invocation);

static void entries_changed_cb        (MwsScheduler    *scheduler,
                                       GPtrArray       *added,
                                       GPtrArray       *removed,
                                       gpointer         user_data);
static void active_entries_changed_cb (MwsScheduler    *scheduler,
                                       GPtrArray       *added,
                                       GPtrArray       *removed,
                                       gpointer         user_data);
static void entry_notify_cb           (GObject         *obj,
                                       GParamSpec      *pspec,
                                       gpointer         user_data);

static const GDBusErrorEntry scheduler_error_map[] =
  {
    { MWS_SCHEDULER_ERROR_FULL, "com.endlessm.DownloadManager1.Scheduler.Error.Full" },
    { MWS_SCHEDULER_ERROR_IDENTIFYING_PEER,
      "com.endlessm.DownloadManager1.Scheduler.Error.IdentifyingPeer" },
  };
G_STATIC_ASSERT (G_N_ELEMENTS (scheduler_error_map) == MWS_SCHEDULER_N_ERRORS);
G_STATIC_ASSERT (G_N_ELEMENTS (scheduler_error_map) == G_N_ELEMENTS (scheduler_errors));

/**
 * MwsScheduleService:
 *
 * An implementation of a D-Bus interface to expose the download scheduler and
 * all its schedule entries on the bus. This will expose all the necessary
 * objects on the bus for peers to interact with them, and hooks them up to
 * internal state management using #MwsScheduleService:scheduler.
 *
 * Since: 0.1.0
 */
struct _MwsScheduleService
{
  GObject parent;

  GDBusConnection *connection;  /* (owned) */
  gchar *object_path;  /* (owned) */
  guint entry_subtree_id;

  /* Used to cancel any pending operations when the object is unregistered. */
  GCancellable *cancellable;  /* (owned) */

  MwsScheduler *scheduler;  /* (owned) */
};

typedef enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_SCHEDULER,
  PROP_BUSY,
} MwsScheduleServiceProperty;

G_DEFINE_TYPE (MwsScheduleService, mws_schedule_service, G_TYPE_OBJECT)

static void
mws_schedule_service_class_init (MwsScheduleServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_BUSY + 1] = { NULL, };

  object_class->constructed = mws_schedule_service_constructed;
  object_class->dispose = mws_schedule_service_dispose;
  object_class->get_property = mws_schedule_service_get_property;
  object_class->set_property = mws_schedule_service_set_property;

  /**
   * MwsScheduleService:connection:
   *
   * D-Bus connection to export objects on.
   *
   * Since: 0.1.0
   */
  props[PROP_CONNECTION] =
      g_param_spec_object ("connection", "Connection",
                           "D-Bus connection to export objects on.",
                           G_TYPE_DBUS_CONNECTION,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleService:object-path:
   *
   * Object path to root all exported objects at. If this does not end in a
   * slash, one will be added.
   *
   * Since: 0.1.0
   */
  props[PROP_OBJECT_PATH] =
      g_param_spec_string ("object-path", "Object Path",
                           "Object path to root all exported objects at.",
                           "/",
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleService:scheduler:
   *
   * Scheduler to contain and order all schedule entries.
   *
   * Since: 0.1.0
   */
  props[PROP_SCHEDULER] =
      g_param_spec_object ("scheduler", "Scheduler",
                           "Scheduler to contain and order all schedule entries.",
                           MWS_TYPE_SCHEDULER,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleService:busy:
   *
   * %TRUE if the D-Bus API is busy; for example if there are currently any
   * schedule entries exposed on the bus.
   *
   * Since: 0.1.0
   */
  props[PROP_BUSY] =
      g_param_spec_boolean ("busy", "Busy",
                            "%TRUE if the D-Bus API is busy.",
                            FALSE,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /* Error domain registration for D-Bus. We do this here, rather than in a
   * #GOnce section in mws_scheduler_error_quark(), to avoid spreading the D-Bus
   * code outside this file. */
  for (gsize i = 0; i < G_N_ELEMENTS (scheduler_error_map); i++)
    g_dbus_error_register_error (MWS_SCHEDULER_ERROR,
                                 scheduler_error_map[i].error_code,
                                 scheduler_error_map[i].dbus_error_name);
}

static void
mws_schedule_service_init (MwsScheduleService *self)
{
  self->cancellable = g_cancellable_new ();
}

static GPtrArray *
hash_table_get_values_as_ptr_array (GHashTable *table)
{
  GHashTableIter iter;
  gpointer value;

  g_autoptr(GPtrArray) values = g_ptr_array_new_with_free_func (NULL);
  g_hash_table_iter_init (&iter, table);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    g_ptr_array_add (values, value);

  return g_steal_pointer (&values);
}

static void
mws_schedule_service_constructed (GObject *object)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (object);

  /* Chain up. */
  G_OBJECT_CLASS (mws_schedule_service_parent_class)->constructed (object);

  /* Expose the initial set of schedule entries. */
  GHashTable *entries;  /* (element-type utf8 MwsScheduleEntry) */
  entries = mws_scheduler_get_entries (self->scheduler);

  g_autoptr(GPtrArray) entries_array = hash_table_get_values_as_ptr_array (entries);
  entries_changed_cb (self->scheduler, entries_array, NULL, self);
}

static void
mws_schedule_service_dispose (GObject *object)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (object);

  g_assert (self->entry_subtree_id == 0);

  /* Disconnect from signals from the scheduler, and from any remaining
   * schedule entries. */
  if (self->scheduler != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->scheduler, self);

      GHashTable *entries;  /* (element-type utf8 MwsScheduleEntry) */
      entries = mws_scheduler_get_entries (self->scheduler);

      g_autoptr(GPtrArray) entries_array = hash_table_get_values_as_ptr_array (entries);
      entries_changed_cb (self->scheduler, NULL, entries_array, self);
    }

  g_clear_object (&self->scheduler);

  g_clear_object (&self->connection);
  g_clear_pointer (&self->object_path, g_free);
  g_clear_object (&self->cancellable);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_schedule_service_parent_class)->dispose (object);
}

static void
mws_schedule_service_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (object);

  switch ((MwsScheduleServiceProperty) property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->object_path);
      break;
    case PROP_SCHEDULER:
      g_value_set_object (value, self->scheduler);
      break;
    case PROP_BUSY:
      g_value_set_boolean (value, mws_schedule_service_get_busy (self));
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_schedule_service_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (object);

  switch ((MwsScheduleServiceProperty) property_id)
    {
    case PROP_CONNECTION:
      /* Construct only. */
      g_assert (self->connection == NULL);
      self->connection = g_value_dup_object (value);
      break;
    case PROP_OBJECT_PATH:
      /* Construct only. */
      g_assert (self->object_path == NULL);
      g_assert (g_variant_is_object_path (g_value_get_string (value)));
      self->object_path = g_value_dup_string (value);
      break;
    case PROP_SCHEDULER: {
      /* Construct only. */
      g_assert (self->scheduler == NULL);
      self->scheduler = g_value_dup_object (value);

      /* Connect to signals from the scheduler. Connect to the initial
       * set of schedule entries in constructed(). */
      g_signal_connect (self->scheduler, "entries-changed",
                        (GCallback) entries_changed_cb, self);
      g_signal_connect (self->scheduler, "active-entries-changed",
                        (GCallback) active_entries_changed_cb, self);

      break;
    }
    case PROP_BUSY:
      /* Read only. Fall through. */
    default:
      g_assert_not_reached ();
    }
}

static MwsScheduleEntry *
object_path_to_schedule_entry (MwsScheduleService *self,
                               const gchar        *object_path)
{
  /* Convert the object path into a schedule entry ID and check it’s known to
   * the scheduler. */
  if (!mws_schedule_entry_id_is_valid (object_path))
    return NULL;

  return mws_scheduler_get_entry (self->scheduler, object_path);
}

static gchar *
schedule_entry_to_object_path (MwsScheduleService *self,
                               MwsScheduleEntry   *entry)
{
  return g_strconcat (self->object_path, "/", mws_schedule_entry_get_id (entry), NULL);
}

static void
count_entries (MwsScheduleService *self,
               guint32            *out_entry_count,
               guint32            *out_active_entry_count)
{
  GHashTable *entries;  /* (element-type utf8 MwsScheduleEntry) */
  entries = mws_scheduler_get_entries (self->scheduler);

  GHashTableIter iter;
  gpointer value;
  guint32 active_entries = 0;

  g_assert (out_entry_count != NULL);
  g_assert (out_active_entry_count != NULL);

  g_hash_table_iter_init (&iter, entries);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (value);

      if (mws_scheduler_is_entry_active (self->scheduler, entry))
        active_entries++;
    }

  *out_entry_count = (guint32) g_hash_table_size (entries);
  *out_active_entry_count = active_entries;

  g_assert (*out_active_entry_count <= *out_entry_count);
}

static void
notify_scheduler_properties (MwsScheduleService *self)
{
  g_auto(GVariantDict) changed_properties_dict = G_VARIANT_DICT_INIT (NULL);
  guint32 entries, active_entries;

  count_entries (self, &entries, &active_entries);

  g_variant_dict_insert (&changed_properties_dict,
                         "ActiveEntryCount", "u", active_entries);
  g_variant_dict_insert (&changed_properties_dict,
                         "EntryCount", "u", entries);

  g_autoptr(GVariant) parameters = NULL;
  parameters = g_variant_ref_sink (
      g_variant_new ("(s@a{sv}as)",
                     "com.endlessm.DownloadManager1.Scheduler",
                     g_variant_dict_end (&changed_properties_dict),
                     NULL));

  g_autoptr(GError) local_error = NULL;
  g_dbus_connection_emit_signal (self->connection,
                                 NULL,  /* broadcast */
                                 "/com/endlessm/DownloadManager1/Scheduler",
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 parameters,
                                 &local_error);
  if (local_error != NULL)
    g_debug ("Error emitting PropertiesChanged signal: %s",
             local_error->message);
}

static void
entries_changed_cb (MwsScheduler *scheduler,
                    GPtrArray    *added,
                    GPtrArray    *removed,
                    gpointer      user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);

  /* Update signal subscriptions for the added and removed entries. */
  for (gsize i = 0; removed != NULL && i < removed->len; i++)
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (removed->pdata[i]);

      g_message ("Removing schedule entry ‘%s’.", mws_schedule_entry_get_id (entry));

      g_signal_handlers_disconnect_by_data (entry, self);
    }

  for (gsize i = 0; added != NULL && i < added->len; i++)
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (added->pdata[i]);

      g_message ("Adding schedule entry ‘%s’.", mws_schedule_entry_get_id (entry));

      g_signal_connect (entry, "notify",
                        (GCallback) entry_notify_cb, self);
    }

  /* The com.endlessm.DownloadManager1.Scheduler properties potentially changed */
  if (((added == NULL) != (removed == NULL)) ||
      (added != NULL && removed != NULL && added->len != removed->len))
    {
      notify_scheduler_properties (self);
    }

  /* This will potentially have changed. */
  g_object_notify (G_OBJECT (self), "busy");
}

static void
emit_download_now_changed (MwsScheduleService *self,
                           GPtrArray          *entries,
                           gboolean            download_now)
{
  g_auto(GVariantDict) changed_properties_dict = G_VARIANT_DICT_INIT (NULL);
  g_variant_dict_insert (&changed_properties_dict,
                         "DownloadNow", "b", download_now);

  g_autoptr(GVariant) parameters = NULL;
  parameters = g_variant_ref_sink (
      g_variant_new ("(s@a{sv}as)",
                     "com.endlessm.DownloadManager1.ScheduleEntry",
                     g_variant_dict_end (&changed_properties_dict),
                     NULL));

  for (gsize i = 0; entries != NULL && i < entries->len; i++)
    {
      MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (entries->pdata[i]);
      g_autoptr(GError) local_error = NULL;

      g_message ("Notifying entry ‘%s’ as %s.",
                 mws_schedule_entry_get_id (entry),
                 download_now ? "active" : "inactive");

      g_autofree gchar *entry_path = schedule_entry_to_object_path (self, entry);

      g_dbus_connection_emit_signal (self->connection,
                                     mws_schedule_entry_get_owner (entry),
                                     entry_path,
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged",
                                     parameters,
                                     &local_error);
      if (local_error != NULL)
        g_debug ("Error emitting PropertiesChanged signal: %s",
                 local_error->message);
    }
}

static void
active_entries_changed_cb (MwsScheduler *scheduler,
                           GPtrArray    *added,
                           GPtrArray    *removed,
                           gpointer      user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);

  /* These entries have become inactive (told to stop downloading).
   * Signal that on the bus. */
  emit_download_now_changed (self, removed, FALSE);

  /* These entries have become active (told they can start downloading).
   * Signal that on the bus. */
  emit_download_now_changed (self, added, TRUE);

  /* The com.endlessm.DownloadManager1.Scheduler properties potentially changed */
  if (((added == NULL) != (removed == NULL)) ||
      (added != NULL && removed != NULL && added->len != removed->len))
    {
      notify_scheduler_properties (self);
    }
}

static void
entry_notify_cb (GObject    *obj,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);
  MwsScheduleEntry *entry = MWS_SCHEDULE_ENTRY (obj);
  g_autoptr(GError) local_error = NULL;

  /* Propagate the signal as a D-Bus signal. */
  const gchar *property_name = g_param_spec_get_name (pspec);
  g_auto(GVariantDict) changed_properties_dict = G_VARIANT_DICT_INIT (NULL);

  if (g_str_equal (property_name, "priority"))
    g_variant_dict_insert (&changed_properties_dict,
                           "Priority", "u", mws_schedule_entry_get_priority (entry));
  else if (g_str_equal (property_name, "resumable"))
    g_variant_dict_insert (&changed_properties_dict,
                           "Resumable", "b", mws_schedule_entry_get_resumable (entry));
  else
    /* Unrecognised property. */
    return;

  g_autofree gchar *entry_path = schedule_entry_to_object_path (self, entry);

  g_autoptr(GVariant) parameters = NULL;
  parameters = g_variant_ref_sink (
      g_variant_new ("(s@a{sv}as)",
                     "com.endlessm.DownloadManager1.ScheduleEntry",
                     g_variant_dict_end (&changed_properties_dict),
                     NULL));
  g_dbus_connection_emit_signal (self->connection,
                                 NULL,  /* broadcast */
                                 entry_path,
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 parameters,
                                 &local_error);
  if (local_error != NULL)
    g_debug ("Error emitting PropertiesChanged signal: %s",
             local_error->message);
}

/**
 * mws_schedule_service_register:
 * @self: a #MwsScheduleService
 * @error: return location for a #GError
 *
 * Register the schedule service objects on D-Bus using the connection details
 * given in #MwsScheduleService:connection and #MwsScheduleService:object-path.
 *
 * Use mws_schedule_service_unregister() to unregister them. Calls to these two
 * functions must be well paired.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_schedule_service_register (MwsScheduleService  *self,
                               GError             **error)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_SERVICE (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  GDBusSubtreeVTable subtree_vtable =
    {
      mws_schedule_service_entry_enumerate,
      mws_schedule_service_entry_introspect,
      mws_schedule_service_entry_dispatch,
    };

  guint id = g_dbus_connection_register_subtree (self->connection,
                                                 self->object_path,
                                                 &subtree_vtable,
                                                 G_DBUS_SUBTREE_FLAGS_NONE,
                                                 g_object_ref (self),
                                                 g_object_unref,
                                                 error);

  if (id == 0)
    return FALSE;

  self->entry_subtree_id = id;

  /* This has potentially changed. */
  g_object_notify (G_OBJECT (self), "busy");

  return TRUE;
}

/**
 * mws_schedule_service_unregister:
 * @self: a #MwsScheduleService
 *
 * Unregister objects from D-Bus which were previously registered using
 * mws_schedule_service_register(). Calls to these two functions must be well
 * paired.
 *
 * Since: 0.1.0
 */
void
mws_schedule_service_unregister (MwsScheduleService *self)
{
  g_return_if_fail (MWS_IS_SCHEDULE_SERVICE (self));

  g_dbus_connection_unregister_subtree (self->connection,
                                        self->entry_subtree_id);
  self->entry_subtree_id = 0;

  /* This has potentially changed. */
  g_object_notify (G_OBJECT (self), "busy");
}

static gchar **
mws_schedule_service_entry_enumerate (GDBusConnection *connection,
                                      const gchar     *sender,
                                      const gchar     *object_path,
                                      gpointer         user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);

  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */
  GHashTable *entries;  /* (element-type utf8 MwsScheduleEntry) */
  entries = mws_scheduler_get_entries (self->scheduler);

  /* Output a list of paths to schedule entry objects. */
  g_autoptr(GPtrArray) paths = NULL;  /* (element-type utf8) */
  paths = g_ptr_array_new_with_free_func (g_free);

  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, entries);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      const gchar *entry_id = key;
      g_ptr_array_add (paths, g_strdup (entry_id));
    }
  g_ptr_array_add (paths, NULL);  /* terminator */

  return (gchar **) g_ptr_array_free (g_steal_pointer (&paths), FALSE);
}

static GDBusInterfaceInfo **
mws_schedule_service_entry_introspect (GDBusConnection *connection,
                                       const gchar     *sender,
                                       const gchar     *object_path,
                                       const gchar     *node,
                                       gpointer         user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);
  g_autofree GDBusInterfaceInfo **interfaces = NULL;

  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */

  if (node == NULL)
    {
      /* The root node implements Scheduler only. */
      interfaces = g_new0 (GDBusInterfaceInfo *, 2);
      interfaces[0] = (GDBusInterfaceInfo *) &scheduler_interface;
      interfaces[1] = NULL;
    }
  else if (object_path_to_schedule_entry (self, node) != NULL)
    {
      /* Build the array of interfaces which are implemented by the schedule
       * entry object. */
      interfaces = g_new0 (GDBusInterfaceInfo *, 2);
      interfaces[0] = (GDBusInterfaceInfo *) &schedule_entry_interface;
      interfaces[1] = NULL;
    }

  return g_steal_pointer (&interfaces);
}

static const GDBusInterfaceVTable *
mws_schedule_service_entry_dispatch (GDBusConnection *connection,
                                     const gchar     *sender,
                                     const gchar     *object_path,
                                     const gchar     *interface_name,
                                     const gchar     *node,
                                     gpointer        *out_user_data,
                                     gpointer         user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);
  static const GDBusInterfaceVTable schedule_entry_interface_vtable =
    {
      mws_schedule_service_entry_method_call,
      NULL,  /* handled in mws_schedule_service_entry_method_call() */
      NULL,  /* handled in mws_schedule_service_entry_method_call() */
    };
  static const GDBusInterfaceVTable scheduler_interface_vtable =
    {
      mws_schedule_service_scheduler_method_call,
      NULL,  /* handled in mws_schedule_service_scheduler_method_call() */
      NULL,  /* handled in mws_schedule_service_scheduler_method_call() */
    };

  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */

  /* Scheduler is implemented on the root of the tree. */
  if (node == NULL &&
      g_str_equal (interface_name, "com.endlessm.DownloadManager1.Scheduler"))
    {
      *out_user_data = user_data;
      return &scheduler_interface_vtable;
    }
  else if (node == NULL)
    {
      return NULL;
    }

  /* We only handle the ScheduleEntry interface on other objects. */
  if (!g_str_equal (interface_name, "com.endlessm.DownloadManager1.ScheduleEntry"))
    return NULL;

  /* Find the schedule entry. */
  MwsScheduleEntry *entry = object_path_to_schedule_entry (self, node);

  if (entry == NULL)
    return NULL;

  *out_user_data = user_data;
  return &schedule_entry_interface_vtable;
}

typedef void (*ScheduleEntryMethodCallFunc) (MwsScheduleService    *self,
                                             MwsScheduleEntry      *entry,
                                             GDBusConnection       *connection,
                                             const gchar           *sender,
                                             GVariant              *parameters,
                                             GDBusMethodInvocation *invocation);

static const struct
  {
    const gchar *interface_name;
    const gchar *method_name;
    ScheduleEntryMethodCallFunc func;
  }
schedule_entry_methods[] =
  {
    /* Handle properties. We have to do this here so we can handle them
     * asynchronously for authorisation checks. */
    { "org.freedesktop.DBus.Properties", "Get",
      mws_schedule_service_entry_properties_get },
    { "org.freedesktop.DBus.Properties", "Set",
      mws_schedule_service_entry_properties_set },
    { "org.freedesktop.DBus.Properties", "GetAll",
      mws_schedule_service_entry_properties_get_all },

    /* Schedule entry methods. */
    { "com.endlessm.DownloadManager1.ScheduleEntry", "Remove",
      mws_schedule_service_entry_remove },
  };

G_STATIC_ASSERT (G_N_ELEMENTS (schedule_entry_methods) ==
                 G_N_ELEMENTS (schedule_entry_interface_methods) +
                 -1  /* NULL terminator */ +
                 3  /* o.fdo.DBus.Properties */);

/* Main handler for incoming D-Bus method calls. */
static void
mws_schedule_service_entry_method_call (GDBusConnection       *connection,
                                        const gchar           *sender,
                                        const gchar           *object_path,
                                        const gchar           *interface_name,
                                        const gchar           *method_name,
                                        GVariant              *parameters,
                                        GDBusMethodInvocation *invocation,
                                        gpointer               user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);

  /* Remove the service prefix from the path. */
  g_assert (g_str_has_prefix (object_path, self->object_path));
  g_assert (object_path[strlen (self->object_path)] == '/');

  MwsScheduleEntry *entry;
  entry = object_path_to_schedule_entry (self,
                                         object_path +
                                         strlen (self->object_path) + 1);
  g_assert (entry != NULL);

  /* Check the @sender is the owner of @entry. */
  if (!g_str_equal (mws_schedule_entry_get_owner (entry), sender))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_OBJECT,
                                             _("Unknown object ‘%s’."), object_path);
      return;
    }

  /* Work out which method to call. */
  for (gsize i = 0; i < G_N_ELEMENTS (schedule_entry_methods); i++)
    {
      if (g_str_equal (schedule_entry_methods[i].interface_name, interface_name) &&
          g_str_equal (schedule_entry_methods[i].method_name, method_name))
        {
          schedule_entry_methods[i].func (self, entry, connection, sender,
                                          parameters, invocation);
          return;
        }
    }

  /* Make sure we actually called a method implementation. GIO guarantees that
   * this function is only called with methods we’ve declared in the interface
   * info, so this should never fail. */
  g_assert_not_reached ();
}

static gboolean
validate_dbus_interface_name (GDBusMethodInvocation *invocation,
                              const gchar           *interface_name)
{
  if (!g_dbus_is_interface_name (interface_name))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                             _("Invalid interface name ‘%s’."),
                                             interface_name);
      return FALSE;
    }

  return TRUE;
}

static void
mws_schedule_service_entry_properties_get (MwsScheduleService    *self,
                                           MwsScheduleEntry      *entry,
                                           GDBusConnection       *connection,
                                           const gchar           *sender,
                                           GVariant              *parameters,
                                           GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_variant_get (parameters, "(&s&s)", &interface_name, &property_name);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Try the property. */
  g_autoptr(GVariant) value = NULL;

  if (g_str_equal (interface_name, "com.endlessm.DownloadManager1.ScheduleEntry"))
    {
      if (g_str_equal (property_name, "Resumable"))
        value = g_variant_new_boolean (mws_schedule_entry_get_resumable (entry));
      else if (g_str_equal (property_name, "Priority"))
        value = g_variant_new_uint32 (mws_schedule_entry_get_priority (entry));
      else if (g_str_equal (property_name, "DownloadNow"))
        value = g_variant_new_boolean (mws_scheduler_is_entry_active (self->scheduler, entry));
    }

  if (value != NULL)
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(v)", g_steal_pointer (&value)));
  else
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                           _("Unknown property ‘%s.%s’."),
                                           interface_name, property_name);
}

static void
mws_schedule_service_entry_properties_set (MwsScheduleService    *self,
                                           MwsScheduleEntry      *entry,
                                           GDBusConnection       *connection,
                                           const gchar           *sender,
                                           GVariant              *parameters,
                                           GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_autoptr(GVariant) value = NULL;
  g_variant_get (parameters, "(&s&sv)", &interface_name, &property_name, &value);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Try the property. */
  gboolean read_only = FALSE, type_error = FALSE, handled = FALSE;

  if (g_str_equal (interface_name, "com.endlessm.DownloadManager1.ScheduleEntry"))
    {
      if (g_str_equal (property_name, "Resumable"))
        {
          if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            {
              mws_schedule_entry_set_resumable (entry, g_variant_get_boolean (value));
              handled = TRUE;
            }
          else
            type_error = TRUE;
        }
      else if (g_str_equal (property_name, "Priority"))
        {
          if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
            {
              mws_schedule_entry_set_priority (entry, g_variant_get_uint32 (value));
              handled = TRUE;
            }
          else
            type_error = TRUE;
        }
      else if (g_str_equal (property_name, "DownloadNow"))
        read_only = TRUE;
    }

  if (read_only)
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_PROPERTY_READ_ONLY,
                                           _("Attribute ‘%s.%s’ is read-only."),
                                           interface_name, property_name);
  else if (type_error)
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_INVALID_SIGNATURE,
                                           _("Invalid type value for property ‘%s.%s’."),
                                           interface_name, property_name);
  else if (handled)
    g_dbus_method_invocation_return_value (invocation, NULL);
  else
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                           _("Unknown property ‘%s.%s’."),
                                           interface_name, property_name);
}

static void
mws_schedule_service_entry_properties_get_all (MwsScheduleService    *self,
                                               MwsScheduleEntry      *entry,
                                               GDBusConnection       *connection,
                                               const gchar           *sender,
                                               GVariant              *parameters,
                                               GDBusMethodInvocation *invocation)
{
  const gchar *interface_name;
  g_variant_get (parameters, "(&s)", &interface_name);

  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Try the interface. */
  g_autoptr(GVariantDict) dict = NULL;

  if (g_str_equal (interface_name, "com.endlessm.DownloadManager1.ScheduleEntry"))
    {
      dict = g_variant_dict_new (NULL);

      g_variant_dict_insert (dict, "Resumable",
                             "b", mws_schedule_entry_get_resumable (entry));
      g_variant_dict_insert (dict, "Priority",
                             "u", mws_schedule_entry_get_priority (entry));
      g_variant_dict_insert (dict, "DownloadNow",
                             "b", mws_scheduler_is_entry_active (self->scheduler, entry));
    }

  if (dict != NULL)
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(@a{sv})",
                                                          g_variant_dict_end (dict)));
  else
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                           _("Unknown interface ‘%s’."),
                                           interface_name);
}

static void
mws_schedule_service_entry_remove (MwsScheduleService    *self,
                                   MwsScheduleEntry      *entry,
                                   GDBusConnection       *connection,
                                   const gchar           *sender,
                                   GVariant              *parameters,
                                   GDBusMethodInvocation *invocation)
{
  g_autoptr(GError) local_error = NULL;

  g_autoptr(GPtrArray) removed = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed, GUINT_TO_POINTER (mws_schedule_entry_get_id (entry)));
  if (mws_scheduler_update_entries (self->scheduler, NULL, removed, &local_error))
    {
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else
    {
      /* We know this error domain is registered with #GDBusError. */
      g_warn_if_fail (local_error->domain == MWS_SCHEDULER_ERROR);
      g_prefix_error (&local_error, _("Error removing entry from scheduler: "));
      g_dbus_method_invocation_return_gerror (invocation, local_error);
    }
}

typedef void (*SchedulerMethodCallFunc) (MwsScheduleService    *self,
                                         GDBusConnection       *connection,
                                         const gchar           *sender,
                                         GVariant              *parameters,
                                         GDBusMethodInvocation *invocation);

static const struct
  {
    const gchar *interface_name;
    const gchar *method_name;
    SchedulerMethodCallFunc func;
  }
scheduler_methods[] =
  {
    /* Handle properties. */
    { "org.freedesktop.DBus.Properties", "Get",
      mws_schedule_service_scheduler_properties_get },
    { "org.freedesktop.DBus.Properties", "Set",
      mws_schedule_service_scheduler_properties_set },
    { "org.freedesktop.DBus.Properties", "GetAll",
      mws_schedule_service_scheduler_properties_get_all },

    /* Scheduler methods. */
    { "com.endlessm.DownloadManager1.Scheduler", "Schedule",
      mws_schedule_service_scheduler_schedule },
  };

G_STATIC_ASSERT (G_N_ELEMENTS (scheduler_methods) ==
                 G_N_ELEMENTS (scheduler_interface_methods) +
                 -1  /* NULL terminator */ +
                 3  /* o.fdo.DBus.Properties */);

static void
mws_schedule_service_scheduler_method_call (GDBusConnection       *connection,
                                            const gchar           *sender,
                                            const gchar           *object_path,
                                            const gchar           *interface_name,
                                            const gchar           *method_name,
                                            GVariant              *parameters,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data)
{
  MwsScheduleService *self = MWS_SCHEDULE_SERVICE (user_data);

  /* FIXME: Add permissions checks? This is the right place to add them.
   * Currently, we rely on D-Bus policy allowing/preventing access from
   * appropriate peers. */

  /* Remove the service prefix from the path. */
  g_assert (g_str_equal (object_path, self->object_path));

  /* Work out which method to call. */
  for (gsize i = 0; i < G_N_ELEMENTS (scheduler_methods); i++)
    {
      if (g_str_equal (scheduler_methods[i].interface_name, interface_name) &&
          g_str_equal (scheduler_methods[i].method_name, method_name))
        {
          scheduler_methods[i].func (self, connection, sender,
                                     parameters, invocation);
          return;
        }
    }

  /* Make sure we actually called a method implementation. GIO guarantees that
   * this function is only called with methods we’ve declared in the interface
   * info, so this should never fail. */
  g_assert_not_reached ();
}

static void
mws_schedule_service_scheduler_properties_get (MwsScheduleService    *self,
                                               GDBusConnection       *connection,
                                               const gchar           *sender,
                                               GVariant              *parameters,
                                               GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_variant_get (parameters, "(&s&s)", &interface_name, &property_name);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  g_autoptr(GVariant) value = NULL;

  if (g_str_equal (interface_name, "com.endlessm.DownloadManager1.Scheduler"))
    {
      guint32 entries, active_entries;

      count_entries (self, &entries, &active_entries);

      if (g_str_equal (property_name, "ActiveEntryCount"))
        value = g_variant_new_uint32 (active_entries);
      else if (g_str_equal (property_name, "EntryCount"))
        value = g_variant_new_uint32 (entries);
    }

  if (value != NULL)
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(v)", g_steal_pointer (&value)));
  else
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                           _("Unknown property ‘%s.%s’."),
                                           interface_name, property_name);
  }

static void
mws_schedule_service_scheduler_properties_set (MwsScheduleService    *self,
                                               GDBusConnection       *connection,
                                               const gchar           *sender,
                                               GVariant              *parameters,
                                               GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_variant_get (parameters, "(&s&sv)", &interface_name, &property_name, NULL);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* No properties exposed. */
  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                         _("Unknown property ‘%s.%s’."),
                                         interface_name, property_name);
}

static void
mws_schedule_service_scheduler_properties_get_all (MwsScheduleService    *self,
                                                   GDBusConnection       *connection,
                                                   const gchar           *sender,
                                                   GVariant              *parameters,
                                                   GDBusMethodInvocation *invocation)
{
  const gchar *interface_name;
  g_variant_get (parameters, "(&s)", &interface_name);

  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Try the interface. */
  if (g_str_equal (interface_name, "com.endlessm.DownloadManager1.Scheduler"))
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new_parsed ("(@a{sv} {})"));
  else
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                           _("Unknown interface ‘%s’."),
                                           interface_name);
}

typedef struct
{
  MwsScheduleService *schedule_service;  /* (unowned) */
  GDBusMethodInvocation *invocation;  /* (owned) */
  MwsScheduleEntry *entry;  /* (owned) */
} ScheduleData;

static ScheduleData *
schedule_data_new (MwsScheduleService    *schedule_service,
                   GDBusMethodInvocation *invocation,
                   MwsScheduleEntry      *entry)
{
  ScheduleData *data = g_new0 (ScheduleData, 1);
  data->schedule_service = schedule_service;
  data->invocation = g_object_ref (invocation);
  data->entry = g_object_ref (entry);
  return data;
}

static void
schedule_data_free (ScheduleData *data)
{
  g_clear_object (&data->invocation);
  g_clear_object (&data->entry);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ScheduleData, schedule_data_free)

static void schedule_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
mws_schedule_service_scheduler_schedule (MwsScheduleService    *self,
                                         GDBusConnection       *connection,
                                         const gchar           *sender,
                                         GVariant              *parameters,
                                         GDBusMethodInvocation *invocation)
{
  g_autoptr(GError) local_error = NULL;

  /* Create a schedule entry, validating the parameters at the time. */
  g_autoptr(GVariant) properties_variant = NULL;
  g_variant_get (parameters, "(@a{sv})", &properties_variant);

  g_autoptr(MwsScheduleEntry) entry = NULL;
  entry = mws_schedule_entry_new_from_variant (sender, properties_variant, &local_error);

  if (entry == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             _("Invalid schedule entry parameters: %s"),
                                             local_error->message);
      return;
    }

  /* Load the peer’s credentials and watch to see if it disappears in future (to
   * allow removing all its schedule entries). The credentials will allow the
   * scheduler to prioritise entries by sender. */
  mws_peer_manager_ensure_peer_credentials_async (mws_scheduler_get_peer_manager (self->scheduler),
                                                  sender, self->cancellable,
                                                  schedule_cb,
                                                  schedule_data_new (self, invocation, entry));
}

static void
schedule_cb (GObject      *obj,
             GAsyncResult *result,
             gpointer      user_data)
{
  MwsPeerManager *peer_manager = MWS_PEER_MANAGER (obj);
  g_autoptr(ScheduleData) data = user_data;
  MwsScheduleService *self = data->schedule_service;
  GDBusMethodInvocation *invocation = data->invocation;
  MwsScheduleEntry *entry = data->entry;
  g_autoptr(GError) local_error = NULL;

  /* Finish looking up the sender. */
  const gchar *sender_path = NULL;
  sender_path = mws_peer_manager_ensure_peer_credentials_finish (peer_manager,
                                                                 result, &local_error);

  if (sender_path == NULL)
    {
      g_prefix_error (&local_error, _("Error adding entry to scheduler: "));
      g_dbus_method_invocation_return_gerror (invocation, local_error);
      return;
    }

  /* Add it to the scheduler. */
  g_autoptr(GPtrArray) added = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added, entry);

  if (!mws_scheduler_update_entries (self->scheduler, added, NULL, &local_error))
    {
      /* We know this error domain is registered with #GDBusError. */
      g_warn_if_fail (local_error->domain == MWS_SCHEDULER_ERROR);
      g_prefix_error (&local_error, _("Error adding entry to scheduler: "));
      g_dbus_method_invocation_return_gerror (invocation, local_error);
      return;
    }

  /* Build a path for the entry and return it. */
  g_autofree gchar *entry_path = schedule_entry_to_object_path (self, entry);
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(o)", entry_path));
}

/**
 * mws_schedule_service_new:
 * @connection: (transfer none): D-Bus connection to export objects on
 * @object_path: root path to export objects below; must be a valid D-Bus object
 *    path
 * @scheduler: (transfer none): scheduler to expose
 *
 * Create a new #MwsScheduleService instance which is set up to run as a
 * service.
 *
 * Returns: (transfer full): a new #MwsScheduleService
 * Since: 0.1.0
 */
MwsScheduleService *
mws_schedule_service_new (GDBusConnection *connection,
                          const gchar     *object_path,
                          MwsScheduler    *scheduler)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (g_variant_is_object_path (object_path), NULL);
  g_return_val_if_fail (MWS_IS_SCHEDULER (scheduler), NULL);

  return g_object_new (MWS_TYPE_SCHEDULE_SERVICE,
                       "connection", connection,
                       "object-path", object_path,
                       "scheduler", scheduler,
                       NULL);
}

/**
 * mws_schedule_service_get_busy:
 * @self: a #MwsScheduleService
 *
 * Get the value of #MwsScheduleService:busy.
 *
 * Returns: %TRUE if the service is busy, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_schedule_service_get_busy (MwsScheduleService *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_SERVICE (self), FALSE);

  GHashTable *entries = mws_scheduler_get_entries (self->scheduler);
  return (self->entry_subtree_id != 0 && g_hash_table_size (entries) > 0);
}
