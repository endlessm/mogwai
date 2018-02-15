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

#include <glib/gi18n-lib.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <libmogwai-schedule/schedule-entry-interface.h>
#include <libmogwai-schedule-client/schedule-entry.h>
#include <string.h>


/* These errors do not need to be registered with
 * g_dbus_error_register_error_domain() as they never go over the bus. */
G_DEFINE_QUARK (MwscScheduleEntryError, mwsc_schedule_entry_error)

static void mwsc_schedule_entry_initable_init       (GInitableIface      *iface);
static void mwsc_schedule_entry_async_initable_init (GAsyncInitableIface *iface);
static void mwsc_schedule_entry_constructed         (GObject             *object);
static void mwsc_schedule_entry_dispose             (GObject             *object);

static void mwsc_schedule_entry_get_property        (GObject             *object,
                                                     guint                property_id,
                                                     GValue              *value,
                                                     GParamSpec          *pspec);
static void mwsc_schedule_entry_set_property        (GObject             *object,
                                                     guint                property_id,
                                                     const GValue        *value,
                                                     GParamSpec          *pspec);

static gboolean mwsc_schedule_entry_init_failable (GInitable            *initable,
                                                   GCancellable         *cancellable,
                                                   GError              **error);
static void     mwsc_schedule_entry_init_async    (GAsyncInitable       *initable,
                                                   int                   io_priority,
                                                   GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
static gboolean mwsc_schedule_entry_init_finish   (GAsyncInitable       *initable,
                                                   GAsyncResult         *result,
                                                   GError              **error);

static void properties_changed_cb      (GDBusProxy  *proxy,
                                        GVariant    *changed_properties,
                                        GStrv        invalidated_properties,
                                        gpointer     user_data);
static void signal_cb                  (GDBusProxy  *proxy,
                                        const gchar *sender_name,
                                        const gchar *signal_name,
                                        GVariant    *parameters,
                                        gpointer     user_data);
static void proxy_notify_name_owner_cb (GObject     *obj,
                                        GParamSpec  *pspec,
                                        gpointer     user_data);

/**
 * MwscScheduleEntry:
 *
 * An entry in the scheduler representing a single download (either active or
 * inactive). This stores the scheduling parameters for the download as provided
 * by this app, and exposes the scheduler’s decision about whether to pause or
 * enable the download at the moment (#MwscScheduleEntry:download-now); whether
 * it is ‘active’.
 *
 * To create an #MwscScheduleEntry, call mwsc_scheduler_schedule_async() with
 * some initial parameters for the download. Once created, the entry may be
 * made active immediately by the scheduler.
 *
 * The parameters for the download can be updated throughout its lifetime (for
 * example, if an initial estimation of the size of the download is updated).
 * Set them on the #MwscScheduleEntry using the mwsc_schedule_entry_*() methods,
 * then send the updates to the service using
 * mwsc_schedule_send_properties_async().
 *
 * Any updates to the properties from the service will be signalled using
 * #GObject::notify and will overwrite any non-uploaded local changes.
 *
 * The schedule entry is active if #MwscScheduleEntry:download-now is %TRUE. It
 * may be %TRUE or %FALSE immediately after the schedule entry is created, and
 * may change value several times over the lifetime of the entry. If it changes
 * value from %TRUE to %FALSE, the app must pause the ongoing download until
 * #MwscScheduleEntry:download-now becomes %TRUE again.
 *
 * Once the download is finished, or if it is cancelled or becomes irrelevant
 * or obsolete, the schedule entry must be removed using
 * mwsc_schedule_entry_remove_async(). This will not automatically happen if
 * the #MwscScheduleEntry instance is finalised.
 *
 * The ID for a #MwscSchedulerEntry is globally unique and never re-used. It’s
 * generated when the #MwscScheduleEntry is created.
 *
 * If the service goes away, or if the schedule entry is removed
 * (mwsc_schedule_entry_remove_async()), #MwscScheduleEntry::invalidated will be
 * emitted, and all future method calls on the object will return a
 * %MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED error.
 *
 * Since: 0.1.0
 */
struct _MwscScheduleEntry
{
  GObject parent;

  GDBusProxy *proxy;  /* (owned); NULL during initialisation */
  GDBusConnection *connection;  /* (owned) */
  gchar *name;  /* (owned); NULL if not running on a message bus */
  gchar *object_path;  /* (owned) */

  /* Exactly one of these will be set after initialisation completes (or
   * fails). */
  GError *init_error;  /* nullable; owned */
  gboolean init_success;
  gboolean initialising;

  gboolean resumable;
  guint32 priority;
};

typedef enum
{
  PROP_CONNECTION = 1,
  PROP_NAME,
  PROP_OBJECT_PATH,
  PROP_PROXY,
  PROP_ID,
  PROP_DOWNLOAD_NOW,
  PROP_RESUMABLE,
  PROP_PRIORITY,
} MwscScheduleEntryProperty;

G_DEFINE_TYPE_WITH_CODE (MwscScheduleEntry, mwsc_schedule_entry, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                mwsc_schedule_entry_initable_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                mwsc_schedule_entry_async_initable_init))

static void
mwsc_schedule_entry_class_init (MwscScheduleEntryClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_PRIORITY + 1] = { NULL, };

  object_class->constructed = mwsc_schedule_entry_constructed;
  object_class->dispose = mwsc_schedule_entry_dispose;
  object_class->get_property = mwsc_schedule_entry_get_property;
  object_class->set_property = mwsc_schedule_entry_set_property;

  /**
   * MwscScheduleEntry:connection:
   *
   * D-Bus connection to proxy the object from.
   *
   * Since: 0.1.0
   */
  props[PROP_CONNECTION] =
      g_param_spec_object ("connection", "Connection",
                           "D-Bus connection to proxy the object from.",
                           G_TYPE_DBUS_CONNECTION,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:name:
   *
   * Well-known or unique name of the peer to proxy the object from. This must
   * be %NULL if and only if the #MwscScheduleEntry:connection is not a message
   * bus connection.
   *
   * Since: 0.1.0
   */
  props[PROP_NAME] =
      g_param_spec_string ("name", "Name",
                           "Well-known or unique name of the peer to proxy the "
                           "object from.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:object-path:
   *
   * Object path to proxy. The object must implement
   * `com.endlessm.DownloadManager1.ScheduleEntry`.
   *
   * Since: 0.1.0
   */
  props[PROP_OBJECT_PATH] =
      g_param_spec_string ("object-path", "Object Path",
                           "Object path to proxy.",
                           "/",
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:proxy:
   *
   * D-Bus proxy to use when interacting with the object. If this is %NULL at
   * construction time, one will be created. If provided, it **must** have
   * cached copies of its properties already.
   *
   * Since: 0.1.0
   */
  props[PROP_PROXY] =
      g_param_spec_object ("proxy", "Proxy",
                           "D-Bus proxy to use when interacting with the object.",
                           G_TYPE_DBUS_PROXY,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:id:
   *
   * The unique, persistent ID for this schedule entry. It’s generated by the
   * scheduler when the entry is first created, and never changes. It has no
   * defined format, other than being a non-empty UTF-8 string.
   *
   * Since: 0.1.0
   */
  props[PROP_ID] =
      g_param_spec_string ("id", "ID",
                           "Unique, persistent ID for this schedule entry.",
                           NULL,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:download-now:
   *
   * Whether the scheduler is currently permitting this download to use the
   * network. If %TRUE, the download should be performed. If %FALSE, it should
   * be paused. The state of this property may change several times during the
   * lifetime of a download.
   *
   * Since: 0.1.0
   */
  props[PROP_DOWNLOAD_NOW] =
      g_param_spec_boolean ("download-now", "Download Now",
                            "Whether the scheduler is currently permitting "
                            "this download to use the network.",
                            FALSE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:resumable:
   *
   * Whether pausing and resuming this download is supported by this application
   * after it’s started. Some applications and servers can only restart
   * downloads from the beginning after pausing them.
   *
   * Since: 0.1.0
   */
  props[PROP_RESUMABLE] =
      g_param_spec_boolean ("resumable", "Resumable",
                            "Whether pausing and resuming this download is supported.",
                            FALSE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * MwscScheduleEntry:priority:
   *
   * The priority of this download relative to others belonging to this
   * application. Higher numbers mean the download is more important.
   *
   * Since: 0.1.0
   */
  props[PROP_PRIORITY] =
      g_param_spec_uint ("priority", "Priority",
                         "The priority of this download relative to others.",
                         0, G_MAXUINT32, 0,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /**
   * MwscScheduleEntry::invalidated:
   * @self: a #MwscScheduleEntry
   * @error: error which caused the schedule entry to be invalidated: currently
   *    %G_DBUS_ERROR_DISCONNECTED or %MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED
   *
   * Emitted when the backing object underlying this #MwscScheduleEntry
   * disappears, or it is otherwise disconnected (due to, for example,
   * providing invalid data). The most common reason for this signal to be
   * emitted is if the underlying D-Bus object disappears.
   *
   * After this signal is emitted, all method calls to #MwscScheduleEntry
   * methods will return %MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED.
   *
   * Since: 0.1.0
   */
  g_signal_new ("invalidated", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 1,
                G_TYPE_ERROR);
}

static void
mwsc_schedule_entry_initable_init (GInitableIface *iface)
{
  iface->init = mwsc_schedule_entry_init_failable;
}

static void
mwsc_schedule_entry_async_initable_init (GAsyncInitableIface *iface)
{
  iface->init_async = mwsc_schedule_entry_init_async;
  iface->init_finish = mwsc_schedule_entry_init_finish;
}

static void
mwsc_schedule_entry_init (MwscScheduleEntry *self)
{
  /* Nothing to do here. */
}

static void
mwsc_schedule_entry_constructed (GObject *object)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (object);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwsc_schedule_entry_parent_class)->constructed (object);

  /* Ensure that :name is %NULL iff :connection is not a message bus
   * connection. */
  gboolean is_message_bus = (g_dbus_connection_get_unique_name (self->connection) != NULL);
  g_assert (is_message_bus == (self->name != NULL));

  /* Check all our construct-only properties are set. */
  g_assert (self->proxy != NULL ||
            (self->connection != NULL &&
             self->name != NULL &&
             self->object_path != NULL));
}

static void
mwsc_schedule_entry_dispose (GObject *object)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (object);

  if (self->proxy != NULL)
    {
      /* Disconnect from signals. */
      g_signal_handlers_disconnect_by_func (self->proxy, properties_changed_cb,
                                            self);
      g_signal_handlers_disconnect_by_func (self->proxy, signal_cb, self);
      g_signal_handlers_disconnect_by_func (self->proxy,
                                            proxy_notify_name_owner_cb, self);
    }

  g_clear_object (&self->proxy);
  g_clear_object (&self->connection);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->object_path, g_free);
  g_clear_error (&self->init_error);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwsc_schedule_entry_parent_class)->dispose (object);
}

static void
mwsc_schedule_entry_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (object);

  switch ((MwscScheduleEntryProperty) property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->connection);
      break;
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->object_path);
      break;
    case PROP_PROXY:
      g_value_set_object (value, self->proxy);
      break;
    case PROP_ID:
      g_value_set_string (value, mwsc_schedule_entry_get_id (self));
      break;
    case PROP_DOWNLOAD_NOW:
      {
        GVariant *variant = g_dbus_proxy_get_cached_property (self->proxy, "DownloadNow");
        g_value_set_boolean (value, g_variant_get_boolean (variant));
        break;
      }
    case PROP_RESUMABLE:
      g_value_set_boolean (value, self->resumable);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, self->priority);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mwsc_schedule_entry_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (object);

  switch ((MwscScheduleEntryProperty) property_id)
    {
    case PROP_CONNECTION:
      /* Construct only. */
      g_assert (self->connection == NULL);
      self->connection = g_value_dup_object (value);
      break;
    case PROP_NAME:
      /* Construct only. */
      g_assert (self->name == NULL);
      g_assert (g_value_get_string (value) == NULL ||
                g_dbus_is_name (g_value_get_string (value)));
      self->name = g_value_dup_string (value);
      break;
    case PROP_OBJECT_PATH:
      /* Construct only. */
      g_assert (self->object_path == NULL);
      g_assert (g_variant_is_object_path (g_value_get_string (value)));
      self->object_path = g_value_dup_string (value);
      break;
    case PROP_PROXY:
      /* Construct only. */
      g_assert (self->proxy == NULL);
      self->proxy = g_value_dup_object (value);
      break;
    case PROP_ID:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_DOWNLOAD_NOW:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_RESUMABLE:
      mwsc_schedule_entry_set_resumable (self, g_value_get_boolean (value));
      break;
    case PROP_PRIORITY:
      mwsc_schedule_entry_set_priority (self, g_value_get_uint (value));
      break;
    default:
      g_assert_not_reached ();
    }
}

/* Report an error with the proxied object's interactions; for example,
 * providing an incorrectly-typed attribute or an invalid update signal. */
static void
schedule_entry_invalidate (MwscScheduleEntry *self,
                           const GError      *error)
{
  g_assert (self->proxy != NULL);

  /* Disconnect from signals. */
  g_signal_handlers_disconnect_by_func (self->proxy, properties_changed_cb,
                                        self);
  g_signal_handlers_disconnect_by_func (self->proxy, signal_cb, self);
  g_signal_handlers_disconnect_by_func (self->proxy,
                                        proxy_notify_name_owner_cb, self);

  /* Clear the proxy, which marks this #MwscScheduleEntry as invalidated. */
  g_debug ("Marking schedule entry ‘%s’ as invalidated due to error: %s",
           mwsc_schedule_entry_get_id (self), error->message);

  g_clear_object (&self->proxy);
  g_object_notify (G_OBJECT (self), "proxy");

  g_signal_emit_by_name (self, "invalidated", error);
}

static gboolean
check_invalidated (MwscScheduleEntry *self,
                   GTask             *task)
{
  /* Invalidated? */
  if (self->proxy == NULL)
    {
      g_task_return_new_error (task, MWSC_SCHEDULE_ENTRY_ERROR,
                               MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED,
                               _("Schedule entry ‘%s’ has been invalidated."),
                               mwsc_schedule_entry_get_id (self));
      return FALSE;
    }

  return TRUE;
}

static void
properties_changed_cb (GDBusProxy *proxy,
                       GVariant   *changed_properties,
                       GStrv       invalidated_properties,
                       gpointer    user_data)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (user_data);

  g_auto(GVariantDict) dict = G_VARIANT_DICT_INIT (changed_properties);

  g_object_freeze_notify (G_OBJECT (self));

  /* Ignore unrecognised properties. */
  gboolean download_now;
  if (g_variant_dict_lookup (&dict, "DownloadNow", "b", &download_now))
    g_object_notify (G_OBJECT (self), "download-now");

  gboolean resumable;
  if (g_variant_dict_lookup (&dict, "Resumable", "b", &resumable))
    mwsc_schedule_entry_set_resumable (self, resumable);

  guint32 priority;
  if (g_variant_dict_lookup (&dict, "Priority", "u", &priority))
    mwsc_schedule_entry_set_priority (self, priority);

  g_object_thaw_notify (G_OBJECT (self));
}

static void
signal_cb (GDBusProxy  *proxy,
           const gchar *sender_name,
           const gchar *signal_name,
           GVariant    *parameters,
           gpointer     user_data)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (user_data);

  /* @sender_name is validated by the #GDBusProxy code so we can trust it. */
  if (g_strcmp0 (signal_name, "Removed") == 0)
    {
      /* Mark the schedule entry as invalidated. */
      g_assert (g_variant_n_children (parameters) == 0);

      g_autoptr(GError) error = NULL;
      g_set_error_literal (&error, MWSC_SCHEDULE_ENTRY_ERROR,
                           MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED,
                           _("Schedule entry was explicitly removed."));
      schedule_entry_invalidate (self, error);
    }
}

static void
proxy_notify_name_owner_cb (GObject    *obj,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (user_data);

  g_debug ("Name owner for proxy ‘%s’ has changed.", self->object_path);

  if (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (obj)) == NULL)
    {
      g_autoptr(GError) error = NULL;

      g_set_error_literal (&error, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED,
                           _("Schedule entry owner has disconnected."));
      schedule_entry_invalidate (self, error);
    }
}

static gboolean
set_up_proxy (MwscScheduleEntry  *self,
              GError            **error)
{
  g_assert (self->proxy != NULL);

  /* Ensure the proxy has its interface info specified, so we can rely on GDBus
   * to check return value types, etc. (See #GDBusProxy:g-interface-info.) */
  if (g_dbus_proxy_get_interface_info (self->proxy) == NULL)
    g_dbus_proxy_set_interface_info (self->proxy,
                                     (GDBusInterfaceInfo *) &schedule_entry_interface);

  /* Subscribe to signals. */
  g_signal_connect (self->proxy, "g-properties-changed",
                    (GCallback) properties_changed_cb, self);
  g_signal_connect (self->proxy, "g-signal", (GCallback) signal_cb, self);
  g_signal_connect (self->proxy, "notify::g-name-owner",
                    (GCallback) proxy_notify_name_owner_cb, self);

  /* Validate that the entry actually exists. */
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) download_now = NULL;

  g_autofree gchar *name_owner = g_dbus_proxy_get_name_owner (self->proxy);

  if (name_owner == NULL)
    {
      g_set_error_literal (&local_error, MWSC_SCHEDULE_ENTRY_ERROR,
                           MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY,
                           _("Schedule entry does not exist on the bus."));
      goto done;
    }

  /* Validate that the properties are cached (use DownloadNow as a proxy for the
   * others). */
  download_now = g_dbus_proxy_get_cached_property (self->proxy, "DownloadNow");

  g_return_val_if_fail (download_now == NULL ||
                        g_variant_is_of_type (download_now, G_VARIANT_TYPE_BOOLEAN),
                        FALSE);

  if (download_now == NULL)
    {
      g_set_error_literal (&local_error, MWSC_SCHEDULE_ENTRY_ERROR,
                           MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY,
                           _("Required DownloadNow property is missing. "
                             "Might not have permission to access the schedule entry."));
      goto done;
    }

done:
  if (local_error != NULL)
    {
      g_propagate_error (error, g_error_copy (local_error));
      g_propagate_error (&self->init_error, g_steal_pointer (&local_error));
      self->init_success = FALSE;
    }
  else
    {
      self->init_success = TRUE;
    }

  return self->init_success;
}

static void
proxy_init_cb (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  MwscScheduleEntry *self = g_task_get_source_object (task);
  g_autoptr(GError) local_error = NULL;

  /* Get the proxy. */
  g_assert (self->proxy == NULL);
  self->proxy = g_dbus_proxy_new_finish (result, &local_error);

  g_assert (self->initialising);
  self->initialising = FALSE;

  if (local_error != NULL)
    {
      g_propagate_error (&self->init_error, g_error_copy (local_error));
      self->init_success = FALSE;
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  if (set_up_proxy (self, &local_error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&local_error));
}

static gboolean
mwsc_schedule_entry_init_failable (GInitable     *initable,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (initable);

  /* For the moment, this only supports the case where we’ve been constructed
   * with a suitable proxy already. */
  if (self->init_error != NULL)
    {
      g_propagate_error (error, g_error_copy (self->init_error));
      return FALSE;
    }
  else if (self->init_success)
    {
      return TRUE;
    }
  else
    {
      return set_up_proxy (self, error);
    }
}

static void
mwsc_schedule_entry_init_async (GAsyncInitable      *initable,
                                int                  io_priority,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  MwscScheduleEntry *self = MWSC_SCHEDULE_ENTRY (initable);

  /* We don’t support parallel initialisation. */
  g_assert (!self->initialising);

  g_autoptr(GTask) task = g_task_new (initable, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_schedule_entry_init_async);

  if (self->init_error != NULL)
    g_task_return_error (task, g_error_copy (self->init_error));
  else if (self->init_success)
    g_task_return_boolean (task, TRUE);
  else
    {
      self->initialising = TRUE;
      g_dbus_proxy_new (self->connection, G_DBUS_PROXY_FLAGS_NONE,
                        (GDBusInterfaceInfo *) &schedule_entry_interface,
                        self->name, self->object_path,
                        "com.endlessm.DownloadManager1.ScheduleEntry",
                        cancellable, proxy_init_cb, g_steal_pointer (&task));
    }
}

static gboolean
mwsc_schedule_entry_init_finish (GAsyncInitable  *initable,
                                 GAsyncResult    *result,
                                 GError         **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * mwsc_schedule_entry_new_from_proxy:
 * @proxy: a #GDBusProxy for a `com.endlessm.DownloadManager1.ScheduleEntry`
 *    object
 * @error: return location for a #GError, or %NULL
 *
 * Create a #MwscScheduleEntry object to wrap the given existing @proxy. The
 * @proxy must have cached all the `ScheduleEntry` properties already.
 *
 * If any of the properties are missing or invalid, an error is returned.
 *
 * Returns: (transfer full): a new #MwscScheduleEntry wrapping @proxy
 * Since: 0.1.0
 */
MwscScheduleEntry *
mwsc_schedule_entry_new_from_proxy (GDBusProxy  *proxy,
                                    GError     **error)
{
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (MWSC_TYPE_SCHEDULE_ENTRY, NULL, error,
                         "connection", g_dbus_proxy_get_connection (proxy),
                         "name", g_dbus_proxy_get_name (proxy),
                         "object-path", g_dbus_proxy_get_object_path (proxy),
                         "proxy", proxy,
                         NULL);
}

/**
 * mwsc_schedule_entry_new_full_async:
 * @connection: D-Bus connection to use
 * @name: (nullable): well-known or unique name of the peer to proxy from, or
 *    %NULL if @connection is not a message bus connection
 * @object_path: path of the object to proxy
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Create a new #MwscScheduleEntry for the given @object_path at @name on
 * @connection, and set up the proxy object. This is an asynchronous process
 * which might fail; object instantiation must be finished (or the error
 * returned) by calling mwsc_schedule_entry_new_full_finish().
 *
 * Since: 0.1.0
 */
void
mwsc_schedule_entry_new_full_async (GDBusConnection     *connection,
                                    const gchar         *name,
                                    const gchar         *object_path,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (name == NULL || g_dbus_is_name (name));
  g_return_if_fail ((g_dbus_connection_get_unique_name (connection) == NULL) ==
                    (name == NULL));
  g_return_if_fail (object_path != NULL &&
                    g_variant_is_object_path (object_path));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_async_initable_new_async (MWSC_TYPE_SCHEDULE_ENTRY, G_PRIORITY_DEFAULT,
                              cancellable, callback, user_data,
                              "connection", connection,
                              "name", name,
                              "object-path", object_path,
                              NULL);
}

/**
 * mwsc_schedule_entry_new_full_finish:
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish initialising a #MwscScheduleEntry. See
 * mwsc_schedule_entry_new_full_async().
 *
 * Returns: (transfer full): initialised #MwscScheduleEntry, or %NULL on error
 * Since: 0.1.0
 */
MwscScheduleEntry *
mwsc_schedule_entry_new_full_finish (GAsyncResult  *result,
                                     GError       **error)
{
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_autoptr(GObject) source_object = g_async_result_get_source_object (result);
  return MWSC_SCHEDULE_ENTRY (g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                                           result, error));
}

/**
 * mwsc_schedule_entry_get_id:
 * @self: a #MwscScheduleEntry
 *
 * Get the persistent identifier for this schedule entry. This is assigned when
 * at construction time, uniquely and persistently, and is never %NULL or the
 * empty string.
 *
 * Returns: identifier for the entry
 * Since: 0.1.0
 */
const gchar *
mwsc_schedule_entry_get_id (MwscScheduleEntry *self)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), NULL);

  const gchar *expected_prefix = "/com/endlessm/DownloadManager1/ScheduleEntry/";
  const gchar *id;

  if (g_str_has_prefix (self->object_path, expected_prefix) &&
      *(self->object_path + strlen (expected_prefix)) != '\0')
    id = self->object_path + strlen (expected_prefix);
  else
    id = self->object_path;

  g_assert (id != NULL && *id != '\0');
  return id;
}

/**
 * mwsc_schedule_entry_get_download_now:
 * @self: a #MwscScheduleEntry
 *
 * Get the value of #MwscScheduleEntry:download-now.
 *
 * Returns: %TRUE if the download is allowed to use the network at the moment,
 *    %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_schedule_entry_get_download_now (MwscScheduleEntry *self)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), FALSE);

  if (self->proxy == NULL)
    return FALSE;

  g_autoptr(GVariant) download_now_variant = NULL;
  download_now_variant = g_dbus_proxy_get_cached_property (self->proxy, "DownloadNow");
  g_assert (download_now_variant != NULL);

  return g_variant_get_boolean (download_now_variant);
}

/**
 * mwsc_schedule_entry_get_priority:
 * @self: a #MwscScheduleEntry
 *
 * Get the value of #MwscScheduleEntry:priority.
 *
 * Returns: the entry’s priority
 * Since: 0.1.0
 */
guint32
mwsc_schedule_entry_get_priority (MwscScheduleEntry *self)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), 0);

  return self->priority;
}

/**
 * mwsc_schedule_entry_set_priority:
 * @self: a #MwscScheduleEntry
 * @priority: the entry’s priority
 *
 * Set the value of #MwscScheduleEntry:priority.
 *
 * Since: 0.1.0
 */
void
mwsc_schedule_entry_set_priority (MwscScheduleEntry *self,
                                  guint32           priority)
{
  g_return_if_fail (MWSC_IS_SCHEDULE_ENTRY (self));

  if (self->priority == priority)
    return;

  self->priority = priority;
  g_object_notify (G_OBJECT (self), "priority");
}

/**
 * mwsc_schedule_entry_get_resumable:
 * @self: a #MwscScheduleEntry
 *
 * Get the value of #MwscScheduleEntry:resumable.
 *
 * Returns: %TRUE if the download is resumable, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_schedule_entry_get_resumable (MwscScheduleEntry *self)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), FALSE);

  return self->resumable;
}

/**
 * mwsc_schedule_entry_set_resumable:
 * @self: a #MwscScheduleEntry
 * @resumable: %TRUE if the download is resumable, %FALSE otherwise
 *
 * Set the value of #MwscScheduleEntry:resumable.
 *
 * Since: 0.1.0
 */
void
mwsc_schedule_entry_set_resumable (MwscScheduleEntry *self,
                                   gboolean          resumable)
{
  g_return_if_fail (MWSC_IS_SCHEDULE_ENTRY (self));

  resumable = !!resumable;
  if (self->resumable == resumable)
    return;

  self->resumable = resumable;
  g_object_notify (G_OBJECT (self), "resumable");
}

typedef struct
{
  guint n_properties;
  gboolean error;
} SendPropertiesData;

static void
send_properties_data_free (SendPropertiesData *data)
{
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SendPropertiesData, send_properties_data_free)

static void queue_send_properties (GDBusProxy         *proxy,
                                   const gchar        *property_name,
                                   GVariant           *property_value,
                                   GCancellable       *cancellable,
                                   GTask              *task,
                                   SendPropertiesData *data);
static void send_properties_cb (GObject      *obj,
                                GAsyncResult *result,
                                gpointer      user_data);

/**
 * mwsc_schedule_entry_send_properties_async:
 * @self: a #MwscScheduleEntry
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Send all locally updated properties to the server. Local changes made with
 * the mwsc_schedule_entry_set_*() functions are not sent to the server until
 * this method is called, in order to allow updates to be batched.
 *
 * (Note, currently the D-Bus API does not allow this batching to be atomic.)
 *
 * If no properties have been changed compared to their values on the server,
 * this is a no-op and will schedule @callback immediately.
 *
 * Since: 0.1.0
 */
void
mwsc_schedule_entry_send_properties_async (MwscScheduleEntry   *self,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULE_ENTRY (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_schedule_entry_send_properties_async);

  if (!check_invalidated (self, task))
    return;

  g_autoptr(SendPropertiesData) data = g_new0 (SendPropertiesData, 1);
  data->n_properties = 1;  /* keep it at 1 for this function to avoid an early return */
  data->error = FALSE;
  g_task_set_task_data (task, data  /* steal */, (GDestroyNotify) send_properties_data_free);

  queue_send_properties (self->proxy,
                         "Priority", g_variant_new_uint32 (self->priority),
                         cancellable, task, data);
  queue_send_properties (self->proxy,
                         "Resumable", g_variant_new_boolean (self->resumable),
                         cancellable, task, data);

  /* Handle a possible early return. */
  send_properties_cb (NULL, NULL, g_steal_pointer (&task));
  data = NULL;  /* stolen above */
}

/* If @property_value is floating, it will be consumed. */
static void
queue_send_properties (GDBusProxy         *proxy,
                       const gchar        *property_name,
                       GVariant           *property_value,
                       GCancellable       *cancellable,
                       GTask              *task,
                       SendPropertiesData *data)
{
  g_autoptr(GVariant) sunk_property_value = g_variant_ref_sink (property_value);

  g_autoptr(GVariant) cached_property_value = NULL;
  cached_property_value = g_dbus_proxy_get_cached_property (proxy, property_name);
  g_assert (cached_property_value != NULL);

  if (g_variant_equal (cached_property_value, sunk_property_value))
    return;

  data->n_properties++;
  g_dbus_connection_call (g_dbus_proxy_get_connection (proxy),
                          g_dbus_proxy_get_name (proxy),
                          g_dbus_proxy_get_object_path (proxy),
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         "com.endlessm.DownloadManager1.ScheduleEntry",
                                         property_name,
                                         sunk_property_value),
                          NULL,  /* no reply type */
                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                          -1,  /* default timeout */
                          cancellable,
                          send_properties_cb,
                          g_object_ref (task));
}

static void
send_properties_cb (GObject      *obj,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  SendPropertiesData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (result != NULL)
    {
      g_autoptr(GVariant) return_value = NULL;
      return_value = g_dbus_connection_call_finish (G_DBUS_CONNECTION (obj),
                                                    result, &error);
    }

  data->n_properties--;
  if (error != NULL)
    data->error = TRUE;

  if (data->n_properties == 0)
    {
      if (data->error)
        g_task_return_new_error (task, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                 _("Error sending updated properties to service."));
      else
        g_task_return_boolean (task, TRUE);
    }
}

/**
 * mwsc_schedule_entry_send_properties_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish sending updated proprties from a #MwscScheduleEntry to the server. See
 * mwsc_schedule_entry_send_properties_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_schedule_entry_send_properties_finish (MwscScheduleEntry  *self,
                                            GAsyncResult       *result,
                                            GError            **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result,
                                                  mwsc_schedule_entry_send_properties_async),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void remove_cb (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data);

/**
 * mwsc_schedule_entry_remove_async:
 * @self: a #MwscScheduleEntry
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Remove this schedule entry from the scheduler. Typically this will be because
 * the associated download has finished; but it could also be because the
 * download has been cancelled or has errored.
 *
 * This will result in the #MwscScheduleEntry::invalidated signal being emitted,
 * and the entry entering the invalidated state; all future calls to
 * asynchronous methods on it will return
 * %MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED.
 *
 * Since: 0.1.0
 */
void
mwsc_schedule_entry_remove_async (MwscScheduleEntry   *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULE_ENTRY (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_schedule_entry_remove_async);

  if (!check_invalidated (self, task))
    return;

  g_dbus_proxy_call (self->proxy,
                     "Remove",
                     NULL,  /* no parameters */
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,  /* default timeout */
                     cancellable,
                     remove_cb,
                     g_steal_pointer (&task));
}

static void
remove_cb (GObject      *obj,
           GAsyncResult *result,
           gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) error = NULL;

  g_autoptr(GVariant) return_value = NULL;
  return_value = g_dbus_proxy_call_finish (proxy, result, &error);

  if (error != NULL)
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);
}

/**
 * mwsc_schedule_entry_remove_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish removing a #MwscScheduleEntry. See mwsc_schedule_entry_remove_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_schedule_entry_remove_finish (MwscScheduleEntry  *self,
                                   GAsyncResult       *result,
                                   GError            **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULE_ENTRY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result,
                                                  mwsc_schedule_entry_remove_async),
                        FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
