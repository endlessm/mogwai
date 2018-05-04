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
#include <libmogwai-schedule/scheduler-interface.h>
#include <libmogwai-schedule-client/schedule-entry.h>
#include <libmogwai-schedule-client/scheduler.h>


/* These errors do not need to be registered with
 * g_dbus_error_register_error_domain() as they never go over the bus. */
G_DEFINE_QUARK (MwscSchedulerError, mwsc_scheduler_error)

static void mwsc_scheduler_initable_init       (GInitableIface      *iface);
static void mwsc_scheduler_async_initable_init (GAsyncInitableIface *iface);
static void mwsc_scheduler_constructed         (GObject             *object);
static void mwsc_scheduler_dispose             (GObject             *object);

static void mwsc_scheduler_get_property        (GObject             *object,
                                                guint                property_id,
                                                GValue              *value,
                                                GParamSpec          *pspec);
static void mwsc_scheduler_set_property        (GObject             *object,
                                                guint                property_id,
                                                const GValue        *value,
                                                GParamSpec          *pspec);

static gboolean mwsc_scheduler_init_failable (GInitable            *initable,
                                              GCancellable         *cancellable,
                                              GError              **error);
static void     mwsc_scheduler_init_async    (GAsyncInitable       *initable,
                                              int                   io_priority,
                                              GCancellable         *cancellable,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);
static gboolean mwsc_scheduler_init_finish   (GAsyncInitable       *initable,
                                              GAsyncResult         *result,
                                              GError              **error);

static void proxy_notify_name_owner_cb (GObject     *obj,
                                        GParamSpec  *pspec,
                                        gpointer     user_data);
static void proxy_properties_changed_cb (GDBusProxy *proxy,
                                         GVariant   *changed_properties,
                                         GStrv       invalidated_properties,
                                         gpointer    user_data);

static const GDBusErrorEntry scheduler_error_map[] =
  {
    { MWSC_SCHEDULER_ERROR_FULL, "com.endlessm.DownloadManager1.Scheduler.Error.Full" },
    { MWSC_SCHEDULER_ERROR_IDENTIFYING_PEER, "com.endlessm.DownloadManager1.Scheduler.Error.IdentifyingPeer" },
  };
G_STATIC_ASSERT (G_N_ELEMENTS (scheduler_error_map) == MWSC_SCHEDULER_N_ERRORS);
G_STATIC_ASSERT (G_N_ELEMENTS (scheduler_error_map) == G_N_ELEMENTS (scheduler_errors));

/**
 * MwscScheduler:
 *
 * A proxy for the scheduler in the D-Bus service. Currently, the only methods
 * available on #MwscScheduler are mwsc_scheduler_schedule_async() and
 * mws_scheduler_schedule_entries_async(), which should
 * be used to create new #MwscScheduleEntrys. See the documentation for those
 * methods for information.
 *
 * If the service goes away, #MwscScheduler::invalidated will be emitted, and
 * all future method calls on the object will return a
 * %MWSC_SCHEDULER_ERROR_INVALIDATED error.
 *
 * Since: 0.1.0
 */
struct _MwscScheduler
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

  guint hold_count;
};

typedef enum
{
  PROP_CONNECTION = 1,
  PROP_NAME,
  PROP_OBJECT_PATH,
  PROP_PROXY,
  PROP_ALLOW_DOWNLOADS,
} MwscSchedulerProperty;

G_DEFINE_TYPE_WITH_CODE (MwscScheduler, mwsc_scheduler, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                mwsc_scheduler_initable_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                mwsc_scheduler_async_initable_init))

static void
mwsc_scheduler_class_init (MwscSchedulerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_ALLOW_DOWNLOADS + 1] = { NULL, };

  object_class->constructed = mwsc_scheduler_constructed;
  object_class->dispose = mwsc_scheduler_dispose;
  object_class->get_property = mwsc_scheduler_get_property;
  object_class->set_property = mwsc_scheduler_set_property;

  /**
   * MwscScheduler:connection:
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
   * MwscScheduler:name:
   *
   * Well-known or unique name of the peer to proxy the object from. This must
   * be %NULL if and only if the #MwscScheduler:connection is not a message
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
   * MwscScheduler:object-path:
   *
   * Object path to proxy. The object must implement
   * `com.endlessm.DownloadManager1.Scheduler`.
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
   * MwscScheduler:proxy:
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
   * MwscScheduler:allow-downloads:
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
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /**
   * MwscScheduler::invalidated:
   * @self: a #MwscScheduler
   * @error: error which caused the scheduler to be invalidated: currently only
   *    %G_DBUS_ERROR_DISCONNECTED
   *
   * Emitted when the backing object underlying this #MwscScheduler disappears,
   * or it is otherwise disconnected (due to, for example, providing invalid
   * data). The most common reason for this signal to be emitted is if the
   * underlying D-Bus object disappears.
   *
   * After this signal is emitted, all method calls to #MwscScheduler methods
   * will return %MWSC_SCHEDULER_ERROR_INVALIDATED.
   *
   * Since: 0.1.0
   */
  g_signal_new ("invalidated", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 1,
                G_TYPE_ERROR);

  /* Error domain registration for D-Bus. We do this here, rather than in a
   * #GOnce section in mwsc_scheduler_error_quark(), because not all
   * #MwscSchedulerErrors map to a D-Bus error. */
  for (gsize i = 0; i < G_N_ELEMENTS (scheduler_error_map); i++)
    g_dbus_error_register_error (MWSC_SCHEDULER_ERROR,
                                 scheduler_error_map[i].error_code,
                                 scheduler_error_map[i].dbus_error_name);
}

static void
mwsc_scheduler_initable_init (GInitableIface *iface)
{
  iface->init = mwsc_scheduler_init_failable;
}

static void
mwsc_scheduler_async_initable_init (GAsyncInitableIface *iface)
{
  iface->init_async = mwsc_scheduler_init_async;
  iface->init_finish = mwsc_scheduler_init_finish;
}

static void
mwsc_scheduler_init (MwscScheduler *self)
{
  /* Nothing to do here. */
}

static void
mwsc_scheduler_constructed (GObject *object)
{
  MwscScheduler *self = MWSC_SCHEDULER (object);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwsc_scheduler_parent_class)->constructed (object);

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
mwsc_scheduler_dispose (GObject *object)
{
  MwscScheduler *self = MWSC_SCHEDULER (object);

  if (self->proxy != NULL)
    {
      /* Disconnect from signals. */
      g_signal_handlers_disconnect_by_func (self->proxy,
                                            proxy_properties_changed_cb, self);
      g_signal_handlers_disconnect_by_func (self->proxy,
                                            proxy_notify_name_owner_cb, self);
    }

  g_clear_object (&self->proxy);
  g_clear_object (&self->connection);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->object_path, g_free);
  g_clear_error (&self->init_error);

  if (self->hold_count > 0)
    g_debug ("Disposing of MwscScheduler with hold count of %u", self->hold_count);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwsc_scheduler_parent_class)->dispose (object);
}

static void
mwsc_scheduler_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  MwscScheduler *self = MWSC_SCHEDULER (object);

  switch ((MwscSchedulerProperty) property_id)
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
    case PROP_ALLOW_DOWNLOADS:
      g_value_set_boolean (value, mwsc_scheduler_get_allow_downloads (self));
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mwsc_scheduler_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  MwscScheduler *self = MWSC_SCHEDULER (object);

  switch ((MwscSchedulerProperty) property_id)
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
    case PROP_ALLOW_DOWNLOADS:
      /* Read only. */
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
    }
}

/* Report an error with the proxied object's interactions; for example,
 * providing an incorrectly-typed attribute or an invalid update signal. */
static void
scheduler_invalidate (MwscScheduler *self,
                      const GError  *error)
{
  g_assert (self->proxy != NULL);

  /* Disconnect from signals. */
  g_signal_handlers_disconnect_by_func (self->proxy,
                                        proxy_properties_changed_cb, self);
  g_signal_handlers_disconnect_by_func (self->proxy,
                                        proxy_notify_name_owner_cb, self);

  /* Clear the proxy, which marks this #MwscScheduler as invalidated. */
  g_debug ("Marking scheduler (%p) as invalidated due to error: %s",
           self, error->message);

  g_clear_object (&self->proxy);
  g_object_notify (G_OBJECT (self), "proxy");

  g_signal_emit_by_name (self, "invalidated", error);
}

static gboolean
check_invalidated (MwscScheduler *self,
                   GTask         *task)
{
  /* Invalidated? */
  if (self->proxy == NULL)
    {
      if (task != NULL)
        g_task_return_new_error (task, MWSC_SCHEDULER_ERROR,
                                 MWSC_SCHEDULER_ERROR_INVALIDATED,
                                 _("Scheduler has been invalidated."));
      return FALSE;
    }

  return TRUE;
}

static void
proxy_notify_name_owner_cb (GObject    *obj,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
  MwscScheduler *self = MWSC_SCHEDULER (user_data);

  g_debug ("Name owner for proxy ‘%s’ has changed.", self->object_path);

  if (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (obj)) == NULL)
    {
      g_autoptr(GError) error = NULL;
      g_set_error_literal (&error, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED,
                           _("Scheduler owner has disconnected."));
      scheduler_invalidate (self, error);
    }
}

static void
proxy_properties_changed_cb (GDBusProxy *proxy,
                             GVariant   *changed_properties,
                             GStrv       invalidated_properties,
                             gpointer    user_data)
{
  MwscScheduler *self = MWSC_SCHEDULER (user_data);

  g_debug ("Properties for proxy ‘%s’ have changed.", self->object_path);

  gboolean downloads_allowed;
  if (g_variant_lookup (changed_properties, "DownloadsAllowed", "b", &downloads_allowed))
    g_object_notify (G_OBJECT (self), "allow-downloads");
}

static gboolean
set_up_proxy (MwscScheduler  *self,
              GError        **error)
{
  g_assert (self->proxy != NULL);

  /* Ensure the proxy has its interface info specified, so we can rely on GDBus
   * to check return value types, etc. (See #GDBusProxy:g-interface-info.) */
  if (g_dbus_proxy_get_interface_info (self->proxy) == NULL)
    g_dbus_proxy_set_interface_info (self->proxy,
                                     (GDBusInterfaceInfo *) &scheduler_interface);

  /* We require property caching to be enabled too. */
  g_return_val_if_fail (!(g_dbus_proxy_get_flags (self->proxy) &
                          G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES), FALSE);

  /* Subscribe to signals. */
  g_signal_connect (self->proxy, "notify::g-name-owner",
                    (GCallback) proxy_notify_name_owner_cb, self);
  g_signal_connect (self->proxy, "g-properties-changed",
                    (GCallback) proxy_properties_changed_cb, self);

  /* Validate that the scheduler actually exists. */
  g_autoptr(GError) local_error = NULL;

  g_autofree gchar *name_owner = g_dbus_proxy_get_name_owner (self->proxy);

  if (name_owner == NULL)
    {
      g_set_error_literal (&local_error, MWSC_SCHEDULER_ERROR,
                           MWSC_SCHEDULER_ERROR_INVALIDATED,
                           _("Scheduler does not exist on the bus."));
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
  MwscScheduler *self = g_task_get_source_object (task);
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
mwsc_scheduler_init_failable (GInitable     *initable,
                              GCancellable  *cancellable,
                              GError       **error)
{
  MwscScheduler *self = MWSC_SCHEDULER (initable);

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
mwsc_scheduler_init_async (GAsyncInitable      *initable,
                           int                  io_priority,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  MwscScheduler *self = MWSC_SCHEDULER (initable);

  /* We don’t support parallel initialisation. */
  g_assert (!self->initialising);

  g_autoptr(GTask) task = g_task_new (initable, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_init_async);

  if (self->init_error != NULL)
    g_task_return_error (task, g_error_copy (self->init_error));
  else if (self->init_success)
    g_task_return_boolean (task, TRUE);
  else
    {
      self->initialising = TRUE;
      g_dbus_proxy_new (self->connection, G_DBUS_PROXY_FLAGS_NONE,
                        (GDBusInterfaceInfo *) &scheduler_interface, self->name,
                        self->object_path, "com.endlessm.DownloadManager1.Scheduler",
                        cancellable, proxy_init_cb, g_steal_pointer (&task));
    }
}

static gboolean
mwsc_scheduler_init_finish (GAsyncInitable  *initable,
                            GAsyncResult    *result,
                            GError         **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * mwsc_scheduler_new_from_proxy:
 * @proxy: a #GDBusProxy for a `com.endlessm.DownloadManager1.Scheduler` object
 * @error: return location for a #GError, or %NULL
 *
 * Create a #MwscScheduler object to wrap the given existing @proxy. The
 * @proxy must have cached all the `Scheduler` properties already (currently,
 * there are none).
 *
 * If any of the properties are missing or invalid, an error is returned.
 *
 * Returns: (transfer full): a new #MwscScheduler wrapping @proxy
 * Since: 0.1.0
 */
MwscScheduler *
mwsc_scheduler_new_from_proxy (GDBusProxy  *proxy,
                               GError     **error)
{
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (MWSC_TYPE_SCHEDULER, NULL, error,
                         "connection", g_dbus_proxy_get_connection (proxy),
                         "name", g_dbus_proxy_get_name (proxy),
                         "object-path", g_dbus_proxy_get_object_path (proxy),
                         "proxy", proxy,
                         NULL);
}

static void get_bus_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data);
static void new_cb     (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data);

/**
 * mwsc_scheduler_new_async:
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Convenience version of mwsc_scheduler_new_full_async() which uses the default
 * D-Bus connection, name and object path.
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_new_async (GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_new_async);
  g_bus_get (G_BUS_TYPE_SYSTEM, cancellable, get_bus_cb, g_steal_pointer (&task));
}

static void
get_bus_cb (GObject      *obj,
            GAsyncResult *result,
            gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  g_autoptr(GDBusConnection) connection = g_bus_get_finish (result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  mwsc_scheduler_new_full_async (connection,
                                 "com.endlessm.MogwaiSchedule1",
                                 "/com/endlessm/DownloadManager1",
                                 cancellable, new_cb, g_steal_pointer (&task));
}

static void
new_cb (GObject      *obj,
        GAsyncResult *result,
        gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) error = NULL;

  g_autoptr(MwscScheduler) scheduler = mwsc_scheduler_new_full_finish (result, &error);

  if (error != NULL)
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_steal_pointer (&scheduler), g_object_unref);
}

/**
 * mwsc_scheduler_new_finish:
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish initialising a #MwscScheduler. See mwsc_scheduler_new_async().
 *
 * Returns: (transfer full): initialised #MwscScheduler, or %NULL on error
 * Since: 0.1.0
 */
MwscScheduler *
mwsc_scheduler_new_finish (GAsyncResult  *result,
                           GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result, mwsc_scheduler_new_async), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * mwsc_scheduler_new_full_async:
 * @connection: D-Bus connection to use
 * @name: (nullable): well-known or unique name of the peer to proxy from, or
 *    %NULL if @connection is not a message bus connection
 * @object_path: path of the object to proxy
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Create a new #MwscScheduler for the given @object_path at @name on
 * @connection, and set up the proxy object. This is an asynchronous process
 * which might fail; object instantiation must be finished (or the error
 * returned) by calling mwsc_scheduler_new_finish().
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_new_full_async (GDBusConnection     *connection,
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

  g_async_initable_new_async (MWSC_TYPE_SCHEDULER, G_PRIORITY_DEFAULT,
                              cancellable, callback, user_data,
                              "connection", connection,
                              "name", name,
                              "object-path", object_path,
                              NULL);
}

/**
 * mwsc_scheduler_new_full_finish:
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish initialising a #MwscScheduler. See mwsc_scheduler_new_full_async().
 *
 * Returns: (transfer full): initialised #MwscScheduler, or %NULL on error
 * Since: 0.1.0
 */
MwscScheduler *
mwsc_scheduler_new_full_finish (GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_autoptr(GObject) source_object = g_async_result_get_source_object (result);
  return MWSC_SCHEDULER (g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                                      result, error));
}

static void schedule_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data);

/**
 * mwsc_scheduler_schedule_async:
 * @self: a #MwscScheduler
 * @parameters: (nullable): #GVariant of type `a{sv}` giving initial parameters
 *    for the schedule entry
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Create a new #MwscScheduleEntry in the scheduler and return it. The entry
 * will be created with the given initial @parameters (which may be %NULL to
 * use the defaults). As soon as the entry is created, the scheduler may
 * schedule it, which it will do by setting #MwscScheduleEntry:download-now to
 * %TRUE.
 *
 * If @parameters is floating, it is consumed.
 *
 * The following @parameters are currently supported:
 *
 *  * `resumable` (`b`): sets #MwscScheduleEntry:resumable
 *  * `priority` (`u`): sets #MwscScheduleEntry:priority
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_schedule_async (MwscScheduler       *self,
                               GVariant            *parameters,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULER (self));
  g_return_if_fail (parameters == NULL ||
                    (g_variant_is_normal_form (parameters) &&
                     g_variant_is_of_type (parameters, G_VARIANT_TYPE_VARDICT)));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_schedule_async);

  if (!check_invalidated (self, task))
    return;

  g_autoptr(GPtrArray) parameters_array = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (parameters_array, parameters);
  mwsc_scheduler_schedule_entries_async (self, parameters_array, cancellable,
                                         schedule_cb, g_steal_pointer (&task));
}

/* Steal the given element from the array. This assumes the array’s free
 * function is g_object_unref().
 *
 * FIXME: Use g_ptr_array_steal_index_fast() when we have a version of GLib
 * supporting it. See: https://bugzilla.gnome.org/show_bug.cgi?id=795376. */
static gpointer
ptr_array_steal_index_fast (GPtrArray *array,
                            guint      index_)
{
  g_ptr_array_set_free_func (array, NULL);
  g_autoptr(GObject) obj = g_ptr_array_remove_index_fast (array, index_);
  g_ptr_array_set_free_func (array, (GDestroyNotify) g_object_unref);

  return g_steal_pointer (&obj);
}

static void
schedule_cb (GObject      *obj,
             GAsyncResult *result,
             gpointer      user_data)
{
  MwscScheduler *self = MWSC_SCHEDULER (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GPtrArray) entries = NULL;

  entries = mwsc_scheduler_schedule_entries_finish (self, result, &local_error);

  if (local_error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  g_assert (entries->len == 1);
  g_task_return_pointer (task, ptr_array_steal_index_fast (entries, 0), g_object_unref);
}

/**
 * mwsc_scheduler_schedule_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish adding a #MwscScheduleEntry. See mwsc_scheduler_schedule_async().
 *
 * Returns: (transfer full): the new #MwscScheduleEntry
 * Since: 0.1.0
 */
MwscScheduleEntry *
mwsc_scheduler_schedule_finish (MwscScheduler  *self,
                                GAsyncResult   *result,
                                GError        **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULER (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result, mwsc_scheduler_schedule_async), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct
{
  gsize n_entries;
  GPtrArray *entries;  /* (element-type MwscScheduleEntry) (owned) */
} ScheduleEntriesData;

static void
schedule_entries_data_free (ScheduleEntriesData *data)
{
  g_clear_pointer (&data->entries, g_ptr_array_unref);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ScheduleEntriesData, schedule_entries_data_free)

static void schedule_entries_cb (GObject      *obj,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void proxy_entries_cb    (GObject      *obj,
                                 GAsyncResult *result,
                                 gpointer      user_data);

/**
 * mwsc_scheduler_schedule_entries_async:
 * @self: a #MwscScheduler
 * @parameters: non-empty array of #GVariants of type `a{sv}` giving initial
 *    parameters for each of the schedule entries
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Create one or more new #MwscScheduleEntrys in the scheduler and return them.
 * The entries will be created with the given initial @parameters (which may be
 * %NULL to use the defaults). As soon as the entries are created, the scheduler
 * may schedule them, which it will do by setting
 * #MwscScheduleEntry:download-now to %TRUE on one or more of them.
 *
 * If any of the #GVariants in @parameters are floating, they are consumed.
 *
 * The following @parameters are currently supported:
 *
 *  * `resumable` (`b`): sets #MwscScheduleEntry:resumable
 *  * `priority` (`u`): sets #MwscScheduleEntry:priority
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_schedule_entries_async (MwscScheduler       *self,
                                       GPtrArray           *parameters,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULER (self));
  g_return_if_fail (parameters != NULL && parameters->len > 0);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_schedule_entries_async);

  if (!check_invalidated (self, task))
    return;

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("(aa{sv})"));
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (gsize i = 0; i < parameters->len; i++)
    {
      GVariant *variant = g_ptr_array_index (parameters, i);

      if (variant == NULL)
        variant = g_variant_new ("a{sv}", NULL);

      g_return_if_fail (g_variant_is_normal_form (variant) &&
                        g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT));

      g_variant_builder_add_value (&builder, variant);
    }

  g_variant_builder_close (&builder);

  g_dbus_proxy_call (self->proxy,
                     "ScheduleEntries",
                     g_variant_builder_end (&builder),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,  /* default timeout */
                     cancellable,
                     schedule_entries_cb,
                     g_steal_pointer (&task));
}

static void
schedule_entries_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  MwscScheduler *self = g_task_get_source_object (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  /* Grab the schedule entries. */
  g_autoptr(GVariant) return_value = NULL;
  return_value = g_dbus_proxy_call_finish (proxy, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Start constructing the entries in parallel. */
  g_autoptr(GVariantIter) iter = NULL;
  const gchar *schedule_entry_path;
  g_variant_get (return_value, "(ao)", &iter);

  /* Set up the return closure. */
  g_autoptr(ScheduleEntriesData) data = g_new0 (ScheduleEntriesData, 1);
  data->n_entries = g_variant_iter_n_children (iter);
  data->entries = g_ptr_array_new_with_free_func (g_object_unref);
  g_task_set_task_data (task, g_steal_pointer (&data),
                        (GDestroyNotify) schedule_entries_data_free);

  while (g_variant_iter_loop (iter, "&o", &schedule_entry_path))
    {
      mwsc_schedule_entry_new_full_async (g_dbus_proxy_get_connection (self->proxy),
                                          g_dbus_proxy_get_name (self->proxy),
                                          schedule_entry_path,
                                          cancellable,
                                          proxy_entries_cb,
                                          g_steal_pointer (&task));
    }
}

static void
proxy_entries_cb (GObject      *obj,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;
  ScheduleEntriesData *data = g_task_get_task_data (task);

  g_autoptr(MwscScheduleEntry) entry = mwsc_schedule_entry_new_full_finish (result, &local_error);

  if (entry == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  g_ptr_array_add (data->entries, g_steal_pointer (&entry));

  if (data->entries->len == data->n_entries && !g_task_had_error (task))
    g_task_return_pointer (task, g_steal_pointer (&data->entries),
                           (GDestroyNotify) g_ptr_array_unref);
}

/**
 * mwsc_scheduler_schedule_entries_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish adding one or more #MwscScheduleEntrys. See
 * mwsc_scheduler_schedule_entries_async().
 *
 * Returns: (transfer full) (element-type MwscScheduleEntry): an non-empty array
 *    of the new #MwscScheduleEntrys
 * Since: 0.1.0
 */
GPtrArray *
mwsc_scheduler_schedule_entries_finish (MwscScheduler  *self,
                                        GAsyncResult   *result,
                                        GError        **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULER (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result, mwsc_scheduler_schedule_entries_async), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void hold_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data);

/**
 * mwsc_scheduler_hold_async:
 * @self: a #MwscScheduler
 * @reason: (nullable): reason for holding the daemon, or %NULL to provide none
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Increment the hold count on the scheduler daemon. While the daemon is held,
 * it will not exit due to inactivity. The daemon is automatically held while a
 * schedule entry is registered with it.
 *
 * This function is typically used to hold the daemon while subscribed to
 * signals from it.
 *
 * Calls to this function must be paired with calls to
 * mwsc_scheduler_release_async() to eventually release the hold. You may call
 * this function many times, and must call mwsc_scheduler_release_async() the
 * same number of times.
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_hold_async (MwscScheduler       *self,
                           const gchar         *reason,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_return_if_fail (self->hold_count < G_MAXUINT);

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_hold_async);

  if (!check_invalidated (self, task))
    return;

  /* Check whether we already hold the scheduler. */
  if (self->hold_count++ > 0)
    {
      g_debug ("Already hold scheduler");
      g_task_return_boolean (task, TRUE);
      return;
    }

  /* Hold the scheduler over D-Bus. */
  g_debug ("Holding scheduler over D-Bus with reason: %s", reason);

  if (reason == NULL)
    reason = "";

  g_dbus_proxy_call (self->proxy,
                     "Hold",
                     g_variant_new ("(s)", reason),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,  /* default timeout */
                     cancellable,
                     hold_cb,
                     g_steal_pointer (&task));
}

static void
hold_cb (GObject      *obj,
         GAsyncResult *result,
         gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  MwscScheduler *self = g_task_get_source_object (task);
  g_autoptr(GError) local_error = NULL;

  /* Check for errors. */
  g_autoptr(GVariant) return_value = NULL;
  return_value = g_dbus_proxy_call_finish (proxy, result, &local_error);

  if (local_error != NULL)
    {
      g_assert (self->hold_count > 0);
      self->hold_count--;
      g_task_return_error (task, g_steal_pointer (&local_error));
    }
  else
    {
      g_task_return_boolean (task, TRUE);
    }
}

/**
 * mwsc_scheduler_hold_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish acquiring a hold on the daemon. See mwsc_scheduler_hold_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_scheduler_hold_finish (MwscScheduler  *self,
                            GAsyncResult   *result,
                            GError        **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, mwsc_scheduler_hold_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void release_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data);

/**
 * mwsc_scheduler_release_async:
 * @self: a #MwscScheduler
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Decrement the hold count on the scheduler daemon. See
 * mwsc_scheduler_hold_async() for information about the concept of holding the
 * daemon.
 *
 * Calls to this function must be paired with calls to
 * mwsc_scheduler_hold_async() to initially acquire the hold. You must call
 * this function as many times as mwsc_scheduler_hold_async() is called.
 *
 * Since: 0.1.0
 */
void
mwsc_scheduler_release_async (MwscScheduler       *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (MWSC_IS_SCHEDULER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_return_if_fail (self->hold_count > 0);

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mwsc_scheduler_release_async);

  if (!check_invalidated (self, task))
    return;

  /* Check whether we would still hold the scheduler after releasing. */
  if (--self->hold_count > 0)
    {
      g_debug ("Still hold scheduler");
      g_task_return_boolean (task, TRUE);
      return;
    }

  /* Release the scheduler over D-Bus. */
  g_debug ("Releasing scheduler over D-Bus");

  g_dbus_proxy_call (self->proxy,
                     "Release",
                     NULL,  /* no arguments */
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,  /* default timeout */
                     cancellable,
                     release_cb,
                     g_steal_pointer (&task));
}

static void
release_cb (GObject      *obj,
            GAsyncResult *result,
            gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  MwscScheduler *self = g_task_get_source_object (task);
  g_autoptr(GError) local_error = NULL;

  /* Check for errors. */
  g_autoptr(GVariant) return_value = NULL;
  return_value = g_dbus_proxy_call_finish (proxy, result, &local_error);

  if (local_error != NULL)
    {
      g_assert (self->hold_count < G_MAXUINT);
      self->hold_count++;
      g_task_return_error (task, g_steal_pointer (&local_error));
    }
  else
    {
      g_task_return_boolean (task, TRUE);
    }
}

/**
 * mwsc_scheduler_release_finish:
 * @self: a #MwscScheduleEntry
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish releasing a hold on the daemon. See mwsc_scheduler_release_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_scheduler_release_finish (MwscScheduler  *self,
                               GAsyncResult   *result,
                               GError        **error)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, mwsc_scheduler_release_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * mwsc_scheduler_get_allow_downloads:
 * @self: a #MwscScheduler
 *
 * Get the value of #MwscScheduler:allow-downloads.
 *
 * Returns: %TRUE if the user has indicated that at least one of the active
 *    network connections should be used for large downloads, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwsc_scheduler_get_allow_downloads (MwscScheduler *self)
{
  g_return_val_if_fail (MWSC_IS_SCHEDULER (self), TRUE);

  if (!check_invalidated (self, NULL))
    return TRUE;

  g_autoptr(GVariant) allow_downloads_variant = NULL;
  allow_downloads_variant = g_dbus_proxy_get_cached_property (self->proxy, "DownloadsAllowed");

  if (allow_downloads_variant == NULL)
    {
      /* The property cache is always expected to be populated. */
      g_critical ("%s: Could not get cached DownloadsAllowed property", G_STRFUNC);
      return TRUE;
    }

  return g_variant_get_boolean (allow_downloads_variant);
}
