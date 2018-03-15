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
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/peer-manager.h>
#include <libmogwai-schedule/peer-manager-dbus.h>
#include <libmogwai-schedule/scheduler.h>
#include <stdlib.h>


static void mws_peer_manager_dbus_peer_manager_init (MwsPeerManagerInterface *iface);
static void mws_peer_manager_dbus_dispose           (GObject                 *object);

static void mws_peer_manager_dbus_get_property (GObject      *object,
                                                guint         property_id,
                                                GValue        *value,
                                                GParamSpec   *pspec);
static void mws_peer_manager_dbus_set_property (GObject      *object,
                                                guint         property_id,
                                                const GValue *value,
                                                GParamSpec   *pspec);

static void         mws_peer_manager_dbus_ensure_peer_credentials_async  (MwsPeerManager       *manager,
                                                                          const gchar          *sender,
                                                                          GCancellable         *cancellable,
                                                                          GAsyncReadyCallback   callback,
                                                                          gpointer              user_data);
static gchar       *mws_peer_manager_dbus_ensure_peer_credentials_finish (MwsPeerManager       *manager,
                                                                          GAsyncResult         *result,
                                                                          GError              **error);
static const gchar *mws_peer_manager_dbus_get_peer_credentials           (MwsPeerManager       *manager,
                                                                          const gchar          *sender);

/**
 * MwsPeerManagerDBus:
 *
 * An implementation of the #MwsPeerManager interface which draws its data
 * from the D-Bus daemon. This is the only expected runtime implementation of
 * the interface, and has only been split out from the interface to allow for
 * easier unit testing of anything which uses it.
 *
 * The credentials of a peer are retrieved from the D-Bus daemon using
 * [`GetConnectionCredentials`](https://dbus.freedesktop.org/doc/dbus-specification.html#bus-messages-get-connection-credentials),
 * and realpath() on `/proc/$pid/exe` to get the absolute path to the executable
 * for each peer, which we use as an identifier for it. This is not atomic, as
 * PIDs can be reused in the time it takes us to query the information, but
 * without an LSM enabled in the kernel and dbus-daemon, it’s the best we can do
 * for identifying processes.
 *
 * Since: 0.1.0
 */
struct _MwsPeerManagerDBus
{
  GObject parent;

  GDBusConnection *connection;  /* (owned) */

  /* Hold the watch IDs of all peers who have added entries at some point. */
  GPtrArray *peer_watch_ids;  /* (owned) */

  /* Cache of peer credentials (currently only the executable path of each peer). */
  GHashTable *peer_credentials;  /* (owned) (element-type utf8 filename) */
};

typedef enum
{
  PROP_CONNECTION = 1,
} MwsPeerManagerDBusProperty;

G_DEFINE_TYPE_WITH_CODE (MwsPeerManagerDBus, mws_peer_manager_dbus, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_PEER_MANAGER,
                                                mws_peer_manager_dbus_peer_manager_init))
static void
mws_peer_manager_dbus_class_init (MwsPeerManagerDBusClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_CONNECTION + 1] = { NULL, };

  object_class->dispose = mws_peer_manager_dbus_dispose;
  object_class->get_property = mws_peer_manager_dbus_get_property;
  object_class->set_property = mws_peer_manager_dbus_set_property;

  /**
   * MwsPeerManagerDBus:connection:
   *
   * D-Bus connection to use for retrieving peer credentials.
   *
   * Since: 0.1.0
   */
  props[PROP_CONNECTION] =
      g_param_spec_object ("connection", "Connection",
                           "D-Bus connection to use for retrieving peer credentials.",
                           G_TYPE_DBUS_CONNECTION,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
mws_peer_manager_dbus_peer_manager_init (MwsPeerManagerInterface *iface)
{
  iface->ensure_peer_credentials_async = mws_peer_manager_dbus_ensure_peer_credentials_async;
  iface->ensure_peer_credentials_finish = mws_peer_manager_dbus_ensure_peer_credentials_finish;
  iface->get_peer_credentials = mws_peer_manager_dbus_get_peer_credentials;
}

static void
watcher_id_free (gpointer data)
{
  g_bus_unwatch_name (GPOINTER_TO_UINT (data));
}

static void
mws_peer_manager_dbus_init (MwsPeerManagerDBus *self)
{
  self->peer_watch_ids = g_ptr_array_new_with_free_func (watcher_id_free);
  self->peer_credentials = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_free);
}

static void
mws_peer_manager_dbus_dispose (GObject *object)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (object);

  g_clear_object (&self->connection);

  g_clear_pointer (&self->peer_credentials, g_hash_table_unref);
  g_clear_pointer (&self->peer_watch_ids, g_ptr_array_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_peer_manager_dbus_parent_class)->dispose (object);
}

static void
mws_peer_manager_dbus_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (object);

  switch ((MwsPeerManagerDBusProperty) property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->connection);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_peer_manager_dbus_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (object);

  switch ((MwsPeerManagerDBusProperty) property_id)
    {
    case PROP_CONNECTION:
      /* Construct only. */
      g_assert (self->connection == NULL);
      self->connection = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
peer_vanished_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (user_data);

  g_debug ("%s: Removing peer credentials for ‘%s’ from cache", G_STRFUNC, name);
  if (g_hash_table_remove (self->peer_credentials, name))
    {
      /* Notify users of this API. */
      g_signal_emit_by_name (self, "peer-vanished", name);
    }
}

/* An async function for getting credentials for D-Bus peers, either by querying
 * the bus, or by getting them from a cache. */
static void ensure_peer_credentials_cb (GObject      *obj,
                                        GAsyncResult *result,
                                        gpointer      user_data);

static void
mws_peer_manager_dbus_ensure_peer_credentials_async (MwsPeerManager      *manager,
                                                     const gchar         *sender,
                                                     GCancellable        *cancellable,
                                                     GAsyncReadyCallback  callback,
                                                     gpointer             user_data)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (manager);

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mws_peer_manager_dbus_ensure_peer_credentials_async);
  g_task_set_task_data (task, g_strdup (sender), g_free);

  /* Look up information about the sender so that we can (for example)
   * prioritise downloads by sender. */
  const gchar *peer_path = mws_peer_manager_get_peer_credentials (manager, sender);

  if (peer_path != NULL)
    {
      g_debug ("%s: Found credentials in cache; path is ‘%s’",
               G_STRFUNC, peer_path);
      g_task_return_pointer (task, g_strdup (peer_path), g_free);
    }
  else
    {
      /* Watch the peer so we can know if/when it disappears. */
      guint watch_id = g_bus_watch_name_on_connection (self->connection, sender,
                                                       G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                       NULL, peer_vanished_cb,
                                                       self, NULL);
      g_ptr_array_add (self->peer_watch_ids, GUINT_TO_POINTER (watch_id));

      /* And query for its credentials. */
      g_dbus_connection_call (self->connection, "org.freedesktop.DBus", "/",
                              "org.freedesktop.DBus", "GetConnectionCredentials",
                              g_variant_new ("(s)", sender),
                              G_VARIANT_TYPE ("(a{sv})"),
                              G_DBUS_CALL_FLAGS_NONE,
                              -1  /* default timeout */,
                              cancellable,
                              ensure_peer_credentials_cb, g_steal_pointer (&task));
    }
}

static void
ensure_peer_credentials_cb (GObject      *obj,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (g_task_get_source_object (task));
  GDBusConnection *connection = G_DBUS_CONNECTION (obj);
  const gchar *sender = g_task_get_task_data (task);
  g_autoptr(GError) local_error = NULL;

  /* Finish looking up the sender. */
  g_autoptr(GVariant) retval = NULL;
  retval = g_dbus_connection_call_finish (connection, result, &local_error);

  if (retval == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  /* From the credentials information from D-Bus, we can get the process ID,
   * and then look up the process name. Note that this is racy (the process
   * ID may get recycled between GetConnectionCredentials() returning and us
   * querying the kernel for the process name), but there’s nothing we can
   * do about that. The correct approach is to use an LSM label as returned
   * by GetConnectionCredentials(), but EOS doesn’t support any LSM.
   * We deliberately look at /proc/$pid/exe, rather than /proc/$pid/cmdline,
   * since processes can modify the latter at runtime. */
  guint process_id;

  g_autoptr(GVariant) credentials = g_variant_get_child_value (retval, 0);

  if (!g_variant_lookup (credentials, "ProcessID", "u", &process_id))
    {
      g_task_return_new_error (task, MWS_SCHEDULER_ERROR,
                               MWS_SCHEDULER_ERROR_IDENTIFYING_PEER,
                               _("Process ID for peer ‘%s’ could not be determined"),
                               sender);
      return;
    }

  g_autofree gchar *pid_str = g_strdup_printf ("%u", process_id);
  g_autofree gchar *proc_pid_exe = g_build_filename ("/proc", pid_str, "exe", NULL);
  char sender_path[PATH_MAX];

  if (realpath (proc_pid_exe, sender_path) == NULL)
    {
      g_task_return_new_error (task, MWS_SCHEDULER_ERROR,
                               MWS_SCHEDULER_ERROR_IDENTIFYING_PEER,
                               _("Executable path for peer ‘%s’ (process ID: %s) "
                                 "could not be determined"),
                               sender, pid_str);
      return;
    }

  g_debug ("%s: Got credentials from D-Bus daemon; path is ‘%s’",
           G_STRFUNC, sender_path);

  g_hash_table_replace (self->peer_credentials,
                        g_strdup (sender), g_strdup (sender_path));

  g_task_return_pointer (task, g_strdup (sender_path), g_free);
}

static gchar *
mws_peer_manager_dbus_ensure_peer_credentials_finish (MwsPeerManager  *manager,
                                                      GAsyncResult    *result,
                                                      GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, manager), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result, mws_peer_manager_dbus_ensure_peer_credentials_async), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static const gchar *
mws_peer_manager_dbus_get_peer_credentials (MwsPeerManager *manager,
                                            const gchar    *sender)
{
  MwsPeerManagerDBus *self = MWS_PEER_MANAGER_DBUS (manager);

  g_debug ("%s: Querying credentials for peer ‘%s’", G_STRFUNC, sender);
  return g_hash_table_lookup (self->peer_credentials, sender);
}

/**
 * mws_peer_manager_dbus_new:
 * @connection: a #GDBusConnection
 *
 * Create a #MwsPeerManagerDBus object to wrap the given existing @connection.
 *
 * Returns: (transfer full): a new #MwsPeerManagerDBus wrapping @connection
 * Since: 0.1.0
 */
MwsPeerManagerDBus *
mws_peer_manager_dbus_new (GDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

  return g_object_new (MWS_TYPE_PEER_MANAGER_DBUS,
                       "connection", connection,
                       NULL);
}
