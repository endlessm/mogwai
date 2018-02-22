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
#include <libmogwai-schedule/connection-monitor.h>
#include <libmogwai-schedule/connection-monitor-nm.h>
#include <NetworkManager.h>


static void mws_connection_monitor_nm_connection_monitor_init (MwsConnectionMonitorInterface *iface);
static void mws_connection_monitor_nm_initable_init           (GInitableIface                *iface);
static void mws_connection_monitor_nm_async_initable_init     (GAsyncInitableIface           *iface);
static void mws_connection_monitor_nm_dispose                 (GObject                       *object);

static void mws_connection_monitor_nm_get_property (GObject      *object,
                                                    guint         property_id,
                                                    GValue        *value,
                                                    GParamSpec   *pspec);
static void mws_connection_monitor_nm_set_property (GObject      *object,
                                                    guint         property_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);

static void active_connection_disconnect_and_unref (gpointer data);
static void device_disconnect_and_unref            (gpointer data);

static void active_connection_added_cb           (NMClient           *client,
                                                  NMActiveConnection *active_connection,
                                                  gpointer            user_data);
static void active_connection_removed_cb         (NMClient           *client,
                                                  NMActiveConnection *active_connection,
                                                  gpointer            user_data);
static void active_connection_devices_changed_cb (GObject            *obj,
                                                  GParamSpec         *pspec,
                                                  gpointer            user_data);
static void device_state_changed_cb              (NMDevice           *device,
                                                  guint               new_state,
                                                  guint               old_state,
                                                  guint               reason,
                                                  gpointer            user_data);
static void device_notify_cb                     (GObject            *obj,
                                                  GParamSpec         *pspec,
                                                  gpointer            user_data);
static void setting_connection_notify_cb         (GObject            *obj,
                                                  GParamSpec         *pspec,
                                                  gpointer            user_data);

static gboolean mws_connection_monitor_nm_init_failable (GInitable            *initable,
                                                         GCancellable         *cancellable,
                                                         GError              **error);
static void     mws_connection_monitor_nm_init_async    (GAsyncInitable       *initable,
                                                         int                   io_priority,
                                                         GCancellable         *cancellable,
                                                         GAsyncReadyCallback   callback,
                                                         gpointer              user_data);
static gboolean mws_connection_monitor_nm_init_finish   (GAsyncInitable       *initable,
                                                         GAsyncResult         *result,
                                                         GError              **error);

static const gchar * const *mws_connection_monitor_nm_get_connection_ids     (MwsConnectionMonitor *monitor);
static gboolean             mws_connection_monitor_nm_get_connection_details (MwsConnectionMonitor *monitor,
                                                                              const gchar          *id,
                                                                              MwsConnectionDetails *out_details);

/**
 * MwsConnectionMonitorNm:
 *
 * An implementation of the #MwsConnectionMonitor interface which draws its data
 * from the NetworkManager D-Bus interface. This implementation is
 * #GAsyncInitable, and must be initialised asynchronously to connect to D-Bus
 * without blocking.
 *
 * The metered status of each connection (#MwsConnectionDetails.metered) is
 * calculated as the pessimisic combination of the metered status of each
 * #NMDevice and #NMSettingConnection associated with that active connection.
 *
 * Since: 0.1.0
 */
struct _MwsConnectionMonitorNm
{
  GObject parent;

  NMClient *client;  /* (owned); NULL during initialisation */

  /* Exactly one of these will be set after initialisation completes (or fails). */
  GError *init_error;  /* (nullable) (owned) */
  gboolean init_success;
  gboolean initialising;

  /* Allow cancelling any pending operations during dispose. */
  GCancellable *cancellable;  /* (owned) */

  /* Track connections in NM. This maps connection ID → connection object. */
  GHashTable *active_connections;  /* (owned) (element-type utf8 NMActiveConnection) */
  GHashTable *devices;  /* (owned) (element-type NMDevice NMDevice) */

  /* This cache should be invalidated whenever @connections is changed.
   * We only own the array, not its contents. */
  const gchar **cached_connection_ids;  /* (owned) (array zero-terminated=1) */
};

typedef enum
{
  PROP_CLIENT = 1,
} MwsConnectionMonitorNmProperty;

G_DEFINE_TYPE_WITH_CODE (MwsConnectionMonitorNm, mws_connection_monitor_nm, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_CONNECTION_MONITOR,
                                                mws_connection_monitor_nm_connection_monitor_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                mws_connection_monitor_nm_initable_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                mws_connection_monitor_nm_async_initable_init))
static void
mws_connection_monitor_nm_class_init (MwsConnectionMonitorNmClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_CLIENT + 1] = { NULL, };

  object_class->dispose = mws_connection_monitor_nm_dispose;
  object_class->get_property = mws_connection_monitor_nm_get_property;
  object_class->set_property = mws_connection_monitor_nm_set_property;

  /**
   * MwsConnectionMonitorNm:client:
   *
   * Proxy to the NetworkManager client interface on D-Bus. This must be
   * provided at construction time, or a new proxy will be built and connected
   * as part of the asynchronous initialization of this class (using
   * #GAsyncInitable).
   *
   * Since: 0.1.0
   */
  props[PROP_CLIENT] =
      g_param_spec_object ("client", "Client",
                           "Proxy to the NetworkManager client interface on D-Bus.",
                           NM_TYPE_CLIENT,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
mws_connection_monitor_nm_connection_monitor_init (MwsConnectionMonitorInterface *iface)
{
  iface->get_connection_ids = mws_connection_monitor_nm_get_connection_ids;
  iface->get_connection_details = mws_connection_monitor_nm_get_connection_details;
}

static void
mws_connection_monitor_nm_initable_init (GInitableIface *iface)
{
  iface->init = mws_connection_monitor_nm_init_failable;
}

static void
mws_connection_monitor_nm_async_initable_init (GAsyncInitableIface *iface)
{
  iface->init_async = mws_connection_monitor_nm_init_async;
  iface->init_finish = mws_connection_monitor_nm_init_finish;
}

static void
mws_connection_monitor_nm_init (MwsConnectionMonitorNm *self)
{
  self->cancellable = g_cancellable_new ();
  self->active_connections = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free,
                                                    active_connection_disconnect_and_unref);
  self->devices = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                         device_disconnect_and_unref, NULL);
  self->cached_connection_ids = NULL;
}

/* Utilities for connection and disconnecting signals on various objects. */
static void connection_connect    (MwsConnectionMonitorNm *self,
                                   NMConnection           *connection,
                                   NMActiveConnection     *active_connection);
static void connection_disconnect (NMConnection           *connection);

static void
active_connection_connect (MwsConnectionMonitorNm *self,
                           NMActiveConnection     *active_connection)
{
  g_signal_connect (active_connection, "notify::devices",
                    (GCallback) active_connection_devices_changed_cb, self);

  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));
  connection_connect (self, connection, active_connection);
}

static void
active_connection_disconnect_and_unref (gpointer data)
{
  NMActiveConnection *active_connection = NM_ACTIVE_CONNECTION (data);

  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));
  connection_disconnect (connection);

  g_signal_handlers_disconnect_matched (active_connection, G_SIGNAL_MATCH_FUNC,
                                        0  /* signal ID */, 0  /* detail */,
                                        NULL  /* closure */,
                                        active_connection_devices_changed_cb,
                                        NULL  /* data */);

  g_object_unref (active_connection);
}

static void
active_connection_disconnect_all_devices (MwsConnectionMonitorNm *self,
                                          NMActiveConnection     *active_connection)
{
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, self->devices);

  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      NMDevice *device = NM_DEVICE (key);

      if (nm_device_get_active_connection (device) == active_connection)
        g_hash_table_iter_remove (&iter);
    }
}

static void
device_connect (MwsConnectionMonitorNm *self,
                NMDevice               *device)
{
  g_signal_connect (device, "state-changed",
                    (GCallback) device_state_changed_cb, self);
  g_signal_connect (device, "notify", (GCallback) device_notify_cb, self);
}

static void
device_disconnect_and_unref (gpointer data)
{
  NMDevice *device = NM_DEVICE (data);

  g_signal_handlers_disconnect_matched (device, G_SIGNAL_MATCH_FUNC,
                                        0  /* signal ID */, 0  /* detail */,
                                        NULL  /* closure */,
                                        device_state_changed_cb, NULL  /* data */);
  g_signal_handlers_disconnect_matched (device, G_SIGNAL_MATCH_FUNC,
                                        0  /* signal ID */, 0  /* detail */,
                                        NULL  /* closure */,
                                        device_notify_cb, NULL  /* data */);

  g_object_unref (device);
}

/* Closure for the #NMSettingConnection::notify signal callback. */
typedef struct
{
  MwsConnectionMonitorNm *connection_monitor;  /* (unowned) (not nullable) */
  NMActiveConnection *active_connection;  /* (owned) (not nullable) */
} SettingNotifyData;

static void setting_notify_data_free (SettingNotifyData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SettingNotifyData, setting_notify_data_free)

static SettingNotifyData *
setting_notify_data_new (MwsConnectionMonitorNm *connection_monitor,
                         NMActiveConnection     *active_connection)
{
  g_autoptr(SettingNotifyData) data = g_new0 (SettingNotifyData, 1);
  data->connection_monitor = connection_monitor;
  data->active_connection = g_object_ref (active_connection);
  return g_steal_pointer (&data);
}

static void
setting_notify_data_free (SettingNotifyData *data)
{
  g_clear_object (&data->active_connection);
  g_free (data);
}

static void
connection_connect (MwsConnectionMonitorNm *self,
                    NMConnection           *connection,
                    NMActiveConnection     *active_connection)
{
  NMSettingConnection *setting = nm_connection_get_setting_connection (connection);
  if (setting != NULL)
    g_signal_connect_data (setting, "notify",
                           (GCallback) setting_connection_notify_cb,
                           setting_notify_data_new (self, active_connection),
                           (GClosureNotify) setting_notify_data_free,
                           0  /* flags */);
}

static void
connection_disconnect (NMConnection *connection)
{
  NMSettingConnection *setting = nm_connection_get_setting_connection (connection);
  if (setting != NULL)
    g_signal_handlers_disconnect_matched (setting,
                                          G_SIGNAL_MATCH_FUNC,
                                          0  /* signal ID */, 0  /* detail */,
                                          NULL  /* closure */,
                                          setting_connection_notify_cb,
                                          NULL  /* data */);
}

static void
mws_connection_monitor_nm_dispose (GObject *object)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  if (self->client != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->client, active_connection_added_cb, self);
      g_signal_handlers_disconnect_by_func (self->client, active_connection_removed_cb, self);
    }

  g_clear_object (&self->client);
  g_clear_error (&self->init_error);

  if (self->active_connections != NULL)
    g_hash_table_remove_all (self->active_connections);
  g_clear_pointer (&self->active_connections, (GDestroyNotify) g_hash_table_unref);
  if (self->devices != NULL)
    g_hash_table_remove_all (self->devices);
  g_clear_pointer (&self->devices, (GDestroyNotify) g_hash_table_unref);
  g_clear_pointer (&self->cached_connection_ids, g_free);  /* we only own the container */

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_connection_monitor_nm_parent_class)->dispose (object);
}

static void
mws_connection_monitor_nm_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (object);

  switch ((MwsConnectionMonitorNmProperty) property_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, self->client);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_connection_monitor_nm_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (object);

  switch ((MwsConnectionMonitorNmProperty) property_id)
    {
    case PROP_CLIENT:
      /* Construct only. */
      g_assert (self->client == NULL);
      self->client = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
set_up_client (MwsConnectionMonitorNm  *self,
               GError                 **error)
{
  g_assert (self->client != NULL);

  /* Subscribe to signals. */
  g_signal_connect (self->client, "active-connection-added",
                    (GCallback) active_connection_added_cb, self);
  g_signal_connect (self->client, "active-connection-removed",
                    (GCallback) active_connection_removed_cb, self);

  /* Query for initial connections. */
  const GPtrArray *active_connections = nm_client_get_active_connections (self->client);
  for (gsize i = 0; i < active_connections->len; i++)
    {
      NMActiveConnection *connection = g_ptr_array_index (active_connections, i);
      active_connection_added_cb (self->client, connection, self);
    }

  self->init_success = TRUE;
  return self->init_success;
}

static gboolean
mws_connection_monitor_nm_init_failable (GInitable     *initable,
                                         GCancellable  *cancellable,
                                         GError       **error)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (initable);

  /* For the moment, this only supports the case where we’ve been constructed
   * with a suitable client already. */
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
      return set_up_client (self, error);
    }
}

static void client_new_cb (GObject      *obj,
                           GAsyncResult *result,
                           gpointer      user_data);

static void
mws_connection_monitor_nm_init_async (GAsyncInitable      *initable,
                                      int                  io_priority,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (initable);

  /* We don’t support parallel initialisation. */
  g_assert (!self->initialising);

  g_autoptr(GTask) task = g_task_new (initable, cancellable, callback, user_data);
  g_task_set_source_tag (task, mws_connection_monitor_nm_init_async);

  if (self->init_error != NULL)
    g_task_return_error (task, g_error_copy (self->init_error));
  else if (self->init_success)
    g_task_return_boolean (task, TRUE);
  else
    {
      self->initialising = TRUE;
      nm_client_new_async (cancellable, client_new_cb, g_steal_pointer (&task));
    }
}

static void
client_new_cb (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  MwsConnectionMonitorNm *self = g_task_get_source_object (task);
  g_autoptr(GError) error = NULL;

  /* Get the client. */
  g_assert (self->client == NULL);
  self->client = nm_client_new_finish (result, &error);

  g_assert (self->initialising);
  self->initialising = FALSE;

  if (error != NULL)
    {
      self->init_success = FALSE;
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (set_up_client (self, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&error));
}

static gboolean
mws_connection_monitor_nm_init_finish (GAsyncInitable  *initable,
                                       GAsyncResult    *result,
                                       GError         **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static const gchar * const *
mws_connection_monitor_nm_get_connection_ids (MwsConnectionMonitor *monitor)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (monitor);

  if (self->cached_connection_ids == NULL)
    self->cached_connection_ids = (const gchar **) g_hash_table_get_keys_as_array (self->active_connections, NULL);

  /* Sanity check. */
  g_assert (g_strv_length ((gchar **) self->cached_connection_ids) ==
            g_hash_table_size (self->active_connections));

  return self->cached_connection_ids;
}

/* Convert a #NMMetered to a #MwsMetered. */
static MwsMetered
mws_metered_from_nm_metered (NMMetered m)
{
  switch (m)
    {
    case NM_METERED_UNKNOWN:
      return MWS_METERED_UNKNOWN;
    case NM_METERED_YES:
      return MWS_METERED_YES;
    case NM_METERED_NO:
      return MWS_METERED_NO;
    case NM_METERED_GUESS_YES:
      return MWS_METERED_GUESS_YES;
    case NM_METERED_GUESS_NO:
      return MWS_METERED_GUESS_NO;
    default:
      g_assert_not_reached ();
    }
}

static gboolean
mws_connection_monitor_nm_get_connection_details (MwsConnectionMonitor *monitor,
                                                  const gchar          *id,
                                                  MwsConnectionDetails *out_details)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (monitor);

  /* Does the connection with @id exist? */
  NMActiveConnection *active_connection = g_hash_table_lookup (self->active_connections, id);
  if (active_connection == NULL)
    return FALSE;

  /* Early return if the connection was found, but the caller doesn’t want the details. */
  if (out_details == NULL)
    return TRUE;

  /* To work out whether the active connection is metered, combine the metered
   * status of all the #NMDevices which form it, plus the metered settings of
   * the active connection’s #NMSettingConnection (if it exists).
   * We are being conservative here, on the assumption
   * that a client could download using any active network connection, not just
   * the primary one. In most cases, the distinction is immaterial as we’d
   * expect only a single active network connection on most machines. */
  MwsMetered connection_metered = MWS_METERED_GUESS_NO;
  MwsMetered devices_metered = MWS_METERED_UNKNOWN;

  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));
  NMSettingConnection *setting = nm_connection_get_setting_connection (connection);

  if (setting != NULL)
    connection_metered =
        mws_metered_from_nm_metered (nm_setting_connection_get_metered (setting));

  const GPtrArray *devices = nm_active_connection_get_devices (active_connection);

  for (gsize i = 0; i < devices->len; i++)
    {
      NMDevice *device = g_ptr_array_index (devices, i);

      /* Sort out metered status. */
      devices_metered = mws_metered_combine_pessimistic (mws_metered_from_nm_metered (nm_device_get_metered (device)),
                                                         devices_metered);
    }

  out_details->metered = mws_metered_combine_pessimistic (devices_metered,
                                                          connection_metered);

  return TRUE;
}

static void
active_connection_added_cb (NMClient           *client,
                            NMActiveConnection *active_connection,
                            gpointer            user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  /* Add to the connection IDs list and emit a signal. */
  const gchar *id = nm_active_connection_get_id (active_connection);
  gboolean changed = g_hash_table_replace (self->active_connections,
                                           g_strdup (id),
                                           g_object_ref (active_connection));

  if (changed)
    {
      g_debug ("%s: Adding active connection ‘%s’.", G_STRFUNC, id);

      g_clear_pointer (&self->cached_connection_ids, g_free);

      /* Monitor the set of #NMDevices exposed by this active connection, so we
       * can see when the devices’ settings change. */
      active_connection_connect (self, active_connection);
      active_connection_devices_changed_cb (G_OBJECT (active_connection), NULL, self);

      /* FIXME: In the future, we may want to handle the primary connection
       * (nm_client_get_primary_connection()) differently from other
       * connections. Probably best to tie that in with the rest of the
       * multi-path stuff. */
      g_autoptr(GPtrArray) added = g_ptr_array_new_with_free_func (NULL);
      g_ptr_array_add (added, (gpointer) id);
      g_signal_emit_by_name (self, "connections-changed", added, NULL);
    }
  else
    {
      /* Say the connection’s details have changed, just in case. */
      g_signal_emit_by_name (self, "connection-details-changed", id);
    }
}

static void
active_connection_removed_cb (NMClient           *client,
                              NMActiveConnection *active_connection,
                              gpointer            user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  /* Remove from the connection IDs list and emit a signal. */
  const gchar *id = nm_active_connection_get_id (active_connection);
  gboolean changed = g_hash_table_remove (self->active_connections, id);

  if (changed)
    {
      g_debug ("%s: Removing active connection ‘%s’.", G_STRFUNC, id);

      g_clear_pointer (&self->cached_connection_ids, g_free);

      active_connection_disconnect_all_devices (self, active_connection);

      g_autoptr(GPtrArray) removed = g_ptr_array_new_with_free_func (NULL);
      g_ptr_array_add (removed, (gpointer) id);
      g_signal_emit_by_name (self, "connections-changed", NULL, removed);
    }
}

static void
active_connection_devices_changed_cb (GObject    *obj,
                                      GParamSpec *pspec,
                                      gpointer    user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);
  NMActiveConnection *active_connection = NM_ACTIVE_CONNECTION (obj);

  g_debug ("%s: Devices changed for active connection ‘%s’.",
           G_STRFUNC, nm_active_connection_get_id (active_connection));

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  /* Disconnect signal handlers from the old devices. */
  active_connection_disconnect_all_devices (self, active_connection);

  /* Connect signal handlers to and query the new devices. */
  const GPtrArray *devices = nm_active_connection_get_devices (active_connection);
  for (gsize i = 0; i < devices->len; i++)
    {
      NMDevice *device = NM_DEVICE (g_ptr_array_index (devices, i));

      device_connect (self, device);
      g_hash_table_replace (self->devices, g_object_ref (device), device);
      device_notify_cb (G_OBJECT (device), NULL, self);
      device_state_changed_cb (device, NM_DEVICE_STATE_UNKNOWN,
                               nm_device_get_state (device),
                               NM_DEVICE_STATE_REASON_NONE, self);
    }
}

static void
device_notify_cb (GObject    *obj,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);
  NMDevice *device = NM_DEVICE (obj);

  NMActiveConnection *active_connection = nm_device_get_active_connection (device);
  const gchar *active_connection_id = NULL;
  active_connection_id = (active_connection != NULL) ? nm_active_connection_get_id (active_connection) : "(none)";

  g_debug ("%s: Device ‘%s’ (active connection ‘%s’) notifying property ‘%s’.",
           G_STRFUNC, nm_device_get_iface (device), active_connection_id,
           (pspec != NULL) ? g_param_spec_get_name (pspec) : "(all)");

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  /* Don’t bother working out what changed; just assume that it will probably
   * change our scheduling. */
  if (active_connection != NULL)
    g_signal_emit_by_name (self, "connection-details-changed", active_connection_id);
}

static void
device_state_changed_cb (NMDevice *device,
                         guint     new_state,
                         guint     old_state,
                         guint     reason,
                         gpointer  user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  NMActiveConnection *active_connection = nm_device_get_active_connection (device);
  const gchar *active_connection_id = NULL;
  active_connection_id = (active_connection != NULL) ? nm_active_connection_get_id (active_connection) : "(none)";

  g_debug ("%s: Device ‘%s’ (active connection ‘%s’) state changed from %u to "
           "%u (reason: %u).",
           G_STRFUNC, nm_device_get_iface (device), active_connection_id,
           old_state, new_state, reason);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  if (active_connection != NULL)
    g_signal_emit_by_name (self, "connection-details-changed", active_connection_id);
}

static void
setting_connection_notify_cb (GObject    *obj,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  SettingNotifyData *data = user_data;

  /* Don’t bother working out what changed; just assume that it will probably
   * change our scheduling. */
  g_signal_emit_by_name (data->connection_monitor, "connection-details-changed",
                         nm_active_connection_get_id (data->active_connection));
}

/**
 * mws_connection_monitor_nm_new_from_client:
 * @client: an #NMClient
 * @error: return location for a #GError, or %NULL
 *
 * Create a #MwsConnectionMonitorNm object to wrap the given existing @client.
 *
 * Returns: (transfer full): a new #MwsConnectionMonitorNm wrapping @client
 * Since: 0.1.0
 */
MwsConnectionMonitorNm *
mws_connection_monitor_nm_new_from_client (NMClient  *client,
                                           GError   **error)
{
  g_return_val_if_fail (NM_IS_CLIENT (client), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (MWS_TYPE_CONNECTION_MONITOR_NM, NULL, error,
                         "client", client,
                         NULL);
}

/**
 * mws_connection_monitor_nm_new_async:
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to invoke on completion
 * @user_data: user data to pass to @callback
 *
 * Create a new #MwsConnectionMonitorNm and set it up. This is an asynchronous
 * process which might fail; object instantiation must be finished (or the error
 * returned) by calling mws_connection_monitor_nm_new_finish().
 *
 * Since: 0.1.0
 */
void
mws_connection_monitor_nm_new_async (GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_async_initable_new_async (MWS_TYPE_CONNECTION_MONITOR_NM, G_PRIORITY_DEFAULT,
                              cancellable, callback, user_data,
                              "client", NULL,
                              NULL);
}

/**
 * mws_connection_monitor_nm_new_finish:
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish initialising a #MwsConnectionMonitorNm. See
 * mws_connection_monitor_nm_new_async().
 *
 * Returns: (transfer full): initialised #MwsConnectionMonitorNm, or %NULL on error
 * Since: 0.1.0
 */
MwsConnectionMonitorNm *
mws_connection_monitor_nm_new_finish (GAsyncResult  *result,
                                      GError       **error)
{
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_autoptr(GObject) source_object = g_async_result_get_source_object (result);
  return MWS_CONNECTION_MONITOR_NM (g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                                                 result, error));
}
