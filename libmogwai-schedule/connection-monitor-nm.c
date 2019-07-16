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
#include <libmogwai-tariff/tariff-loader.h>
#include <libmogwai-tariff/tariff.h>
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

static void active_connection_added_cb           (NMClient           *client,
                                                  NMActiveConnection *active_connection,
                                                  gpointer            user_data);
static void active_connection_removed_cb         (NMClient           *client,
                                                  NMActiveConnection *active_connection,
                                                  gpointer            user_data);
static void active_connection_notify_cb          (GObject            *obj,
                                                  GParamSpec         *pspec,
                                                  gpointer            user_data);
static void active_connection_state_changed_cb   (NMActiveConnection *active_connection,
                                                  guint               new_state,
                                                  guint               reason,
                                                  gpointer            user_data);
static void device_added_cb                      (NMClient           *client,
                                                  NMDevice           *device,
                                                  gpointer            user_data);
static void device_removed_cb                    (NMClient           *client,
                                                  NMDevice           *device,
                                                  gpointer            user_data);
static void device_state_changed_cb              (NMDevice           *device,
                                                  guint               new_state,
                                                  guint               old_state,
                                                  guint               reason,
                                                  gpointer            user_data);
static void device_notify_cb                     (GObject            *obj,
                                                  GParamSpec         *pspec,
                                                  gpointer            user_data);
static void connection_changed_cb                (NMConnection       *connection,
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
 * Several settings from #NMSettingUser are read for each active connection:
 *
 *  * `connection.allow-downloads` (boolean): If `1`, big downloads are allowed
 *    on this connection. If `0`, they are not, and Mogwai will never schedule
 *    downloads on this connection. (Default: `1`.)
 *  * `connection.allow-downloads-when-metered` (boolean): If `1`, big downloads
 *    may be scheduled on this connection, iff it is not metered. If the
 *    connection is metered, or if this setting is `0`, Mogwai will not schedule
 *    downloads on this connection. (Default: `0`.)
 *  * `connection.tariff-enabled` (boolean): If `1`, the tariff string in
 *    `connection.tariff` is parsed and used (and must be present). If `0`, it
 *    is not. (Default: `0`.)
 *  * `connection.tariff` (string): A serialised tariff (see
 *    mwt_tariff_builder_get_tariff_as_variant()) which specifies how the
 *    connection’s properties change over time (for example, bandwidth limits
 *    at certain times of day, or capacity limits). (Default: unset.)
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

  /* This cache should be invalidated whenever
   * nm_client_get_active_connections() is changed. */
  gchar **cached_connection_ids;  /* (owned) (array zero-terminated=1) */
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
  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));

  g_signal_connect (active_connection, "state-changed",
                    (GCallback) active_connection_state_changed_cb, self);
  g_signal_connect (active_connection, "notify", (GCallback) active_connection_notify_cb, self);

  /* @connection may be %NULL if the @active_connection is in state
   * #NM_ACTIVE_CONNECTION_STATE_ACTIVATING */
  if (connection != NULL)
    connection_connect (self, connection, active_connection);
}

static void
active_connection_disconnect (MwsConnectionMonitorNm *self,
                              NMActiveConnection     *active_connection)
{
  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));

  if (connection != NULL)
    connection_disconnect (connection);

  g_signal_handlers_disconnect_by_func (active_connection, active_connection_state_changed_cb, self);
  g_signal_handlers_disconnect_by_func (active_connection, active_connection_notify_cb, self);
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
device_disconnect (MwsConnectionMonitorNm *self,
                   NMDevice               *device)
{
  g_signal_handlers_disconnect_by_func (device, device_state_changed_cb, self);
  g_signal_handlers_disconnect_by_func (device, device_notify_cb, self);
}

/* Closure for the #NMSettingConnection::notify signal callback. */
typedef struct
{
  MwsConnectionMonitorNm *connection_monitor;  /* (unowned) (not nullable) */
  NMActiveConnection *active_connection;  /* (owned) (not nullable) */
} ConnectionChangedData;

static void connection_changed_data_free (ConnectionChangedData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ConnectionChangedData, connection_changed_data_free)

static ConnectionChangedData *
setting_notify_data_new (MwsConnectionMonitorNm *connection_monitor,
                         NMActiveConnection     *active_connection)
{
  g_autoptr(ConnectionChangedData) data = g_new0 (ConnectionChangedData, 1);
  data->connection_monitor = connection_monitor;
  data->active_connection = g_object_ref (active_connection);
  return g_steal_pointer (&data);
}

static void
connection_changed_data_free (ConnectionChangedData *data)
{
  g_clear_object (&data->active_connection);
  g_free (data);
}

static void
connection_changed_data_closure_notify (gpointer  data,
                                        GClosure *closure)
{
  connection_changed_data_free (data);
}

static void
connection_connect (MwsConnectionMonitorNm *self,
                    NMConnection           *connection,
                    NMActiveConnection     *active_connection)
{
  g_signal_connect_data (connection, "changed",
                         (GCallback) connection_changed_cb,
                         setting_notify_data_new (self, active_connection),
                         connection_changed_data_closure_notify,
                         0  /* flags */);
}

static void
connection_disconnect (NMConnection *connection)
{
  g_signal_handlers_disconnect_matched (connection,
                                        G_SIGNAL_MATCH_FUNC,
                                        0  /* signal ID */, 0  /* detail */,
                                        NULL  /* closure */,
                                        connection_changed_cb,
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
      g_signal_handlers_disconnect_by_func (self->client, device_added_cb, self);
      g_signal_handlers_disconnect_by_func (self->client, device_removed_cb, self);
    }

  /* Disconnect from all remaining devices. */
  const GPtrArray *devices = nm_client_get_devices (self->client);
  for (gsize i = 0; i < devices->len; i++)
    {
      NMDevice *device = g_ptr_array_index (devices, i);
      device_removed_cb (self->client, device, self);
    }

  /* Disconnect from all remaining active connections. */
  const GPtrArray *active_connections = nm_client_get_active_connections (self->client);
  for (gsize i = 0; i < active_connections->len; i++)
    {
      NMActiveConnection *active_connection = g_ptr_array_index (active_connections, i);
      active_connection_removed_cb (self->client, active_connection, self);
    }

  g_clear_object (&self->client);
  g_clear_error (&self->init_error);

  g_clear_pointer (&self->cached_connection_ids, g_strfreev);

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
  g_signal_connect (self->client, "device-added",
                    (GCallback) device_added_cb, self);
  g_signal_connect (self->client, "device-removed",
                    (GCallback) device_removed_cb, self);

  /* Query for initial connections. */
  const GPtrArray *active_connections = nm_client_get_active_connections (self->client);
  for (gsize i = 0; i < active_connections->len; i++)
    {
      NMActiveConnection *connection = g_ptr_array_index (active_connections, i);
      active_connection_added_cb (self->client, connection, self);
    }

  /* …and devices. */
  const GPtrArray *devices = nm_client_get_devices (self->client);
  for (gsize i = 0; i < devices->len; i++)
    {
      NMDevice *device = g_ptr_array_index (devices, i);
      device_added_cb (self->client, device, self);
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

  const GPtrArray *active_connections = nm_client_get_active_connections (self->client);

  if (self->cached_connection_ids == NULL)
    {
      g_autoptr(GPtrArray) connection_ids = g_ptr_array_new_full (active_connections->len, g_free);
      for (gsize i = 0; i < active_connections->len; i++)
        {
          NMActiveConnection *active_connection = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_connections, i));
          g_ptr_array_add (connection_ids, g_strdup (nm_active_connection_get_id (active_connection)));
        }
      g_ptr_array_add (connection_ids, NULL);  /* NULL terminator */
      self->cached_connection_ids = (gchar **) g_ptr_array_free (g_steal_pointer (&connection_ids), FALSE);
    }

  /* Sanity check. */
  g_warn_if_fail (g_strv_length ((gchar **) self->cached_connection_ids) == active_connections->len);

  return (const gchar * const *) self->cached_connection_ids;
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

/* Get a boolean configuration value from an #NMSettingUser. Valid values are
 * `0` or `1`. Returns the value; if it wasn’t set in the @setting_user, the
 * @default_value is returned. There is no way to distinguish an unset value
 * from a set value equal to the @default_value. */
static gboolean
setting_user_get_boolean (NMSettingUser *setting_user,
                          const gchar   *key,
                          gboolean       default_value)
{
  const gchar *str = nm_setting_user_get_data (setting_user, key);
  if (str == NULL)
    return default_value;
  else if (g_str_equal (str, "0"))
    return FALSE;
  else if (g_str_equal (str, "1"))
    return TRUE;
  else
    g_warning ("Invalid value ‘%s’ for user setting ‘%s’; expecting ‘0’ or ‘1’",
               str, key);

  return default_value;
}

static gboolean
mws_connection_monitor_nm_get_connection_details (MwsConnectionMonitor *monitor,
                                                  const gchar          *id,
                                                  MwsConnectionDetails *out_details)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (monitor);

  /* Does the connection with @id exist? */
  const GPtrArray *active_connections = nm_client_get_active_connections (self->client);
  NMActiveConnection *active_connection = NULL;
  for (gsize i = 0; i < active_connections->len; i++)
    {
      NMActiveConnection *c = NM_ACTIVE_CONNECTION (g_ptr_array_index (active_connections, i));
      if (g_str_equal (nm_active_connection_get_id (c), id))
        {
          active_connection = c;
          break;
        }
    }

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
  NMSettingConnection *setting = (connection != NULL) ? nm_connection_get_setting_connection (connection) : NULL;

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

  /* Get the connection’s tariff information (if set). These keys/values are
   * set on the #NMSettingUser for the primary connection by
   * gnome-control-center, and may be absent. */
  NMSettingUser *setting_user =
      (connection != NULL) ? NM_SETTING_USER (nm_connection_get_setting (connection, NM_TYPE_SETTING_USER)) : NULL;

  /* TODO: If we want to load a default value from eos-autoupdater.conf (see
   * https://phabricator.endlessm.com/T20818#542708), we should plumb it in here
   * (but do the async loading of the file somewhere else, earlier). */
  const gboolean allow_downloads_when_metered_default = FALSE;
  const gboolean allow_downloads_default = TRUE;

  gboolean allow_downloads_when_metered = allow_downloads_when_metered_default;
  gboolean allow_downloads = allow_downloads_default;
  g_autoptr(MwtTariff) tariff = NULL;

  if (setting_user != NULL)
    {
      allow_downloads_when_metered = setting_user_get_boolean (setting_user,
                                                               "connection.allow-downloads-when-metered",
                                                               allow_downloads_when_metered_default);
      allow_downloads = setting_user_get_boolean (setting_user,
                                                  "connection.allow-downloads",
                                                  allow_downloads_default);

      gboolean tariff_enabled = setting_user_get_boolean (setting_user,
                                                          "connection.tariff-enabled",
                                                          FALSE);
      const gchar *tariff_variant_str;
      tariff_variant_str = nm_setting_user_get_data (setting_user,
                                                     "connection.tariff");

      g_debug ("%s: Connection ‘%s’ has:\n"
               " • connection.allow-downloads-when-metered: %s\n"
               " • connection.allow-downloads: %s\n"
               " • connection.tariff-enabled: %s\n"
               " • connection.tariff: %s",
               G_STRFUNC, id, allow_downloads_when_metered ? "yes" : "no",
               allow_downloads ? "yes" : "no", tariff_enabled ? "yes" : "no",
               tariff_variant_str);

      if (tariff_enabled && tariff_variant_str != NULL)
        {
          g_autoptr(GError) local_error = NULL;
          g_autoptr(GVariant) tariff_variant = NULL;
          tariff_variant = g_variant_parse (NULL, tariff_variant_str,
                                            NULL, NULL, &local_error);
          g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();

          if (tariff_variant != NULL &&
              mwt_tariff_loader_load_from_variant (loader, tariff_variant,
                                                   &local_error))
            tariff = g_object_ref (mwt_tariff_loader_get_tariff (loader));

          if (local_error != NULL)
            {
              g_assert (tariff == NULL);
              g_warning ("connection.tariff contained an invalid tariff ‘%s’: %s",
                         tariff_variant_str, local_error->message);
              g_clear_error (&local_error);
            }
        }
      else if (tariff_enabled && tariff_variant_str == NULL)
        {
          g_warning ("connection.tariff is not set even though "
                     "connection.tariff-enabled is 1");
        }
    }

  out_details->metered = mws_metered_combine_pessimistic (devices_metered,
                                                          connection_metered);
  out_details->allow_downloads_when_metered = allow_downloads_when_metered;
  out_details->allow_downloads = allow_downloads;
  out_details->tariff = g_steal_pointer (&tariff);

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

  g_debug ("%s: Adding active connection ‘%s’.", G_STRFUNC, id);

  g_clear_pointer (&self->cached_connection_ids, g_strfreev);
  active_connection_connect (self, active_connection);

  /* FIXME: In the future, we may want to handle the primary connection
   * (nm_client_get_primary_connection()) differently from other
   * connections. Probably best to tie that in with the rest of the
   * multi-path stuff. */
  g_autoptr(GPtrArray) added = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (added, (gpointer) id);
  g_signal_emit_by_name (self, "connections-changed", added, NULL);
}

static void
active_connection_removed_cb (NMClient           *client,
                              NMActiveConnection *active_connection,
                              gpointer            user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  /* Remove from the connection IDs list and emit a signal. */
  const gchar *id = nm_active_connection_get_id (active_connection);

  g_debug ("%s: Removing active connection ‘%s’.", G_STRFUNC, id);

  g_clear_pointer (&self->cached_connection_ids, g_strfreev);
  active_connection_disconnect (self, active_connection);

  g_autoptr(GPtrArray) removed = g_ptr_array_new_with_free_func (NULL);
  g_ptr_array_add (removed, (gpointer) id);
  g_signal_emit_by_name (self, "connections-changed", NULL, removed);
}

static void
active_connection_notify_cb (GObject    *obj,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);
  NMActiveConnection *active_connection = NM_ACTIVE_CONNECTION (obj);
  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));

  const gchar *active_connection_id = NULL;
  active_connection_id = nm_active_connection_get_id (active_connection);

  g_debug ("%s: Active connection ‘%s’ notifying property ‘%s’.",
           G_STRFUNC, active_connection_id,
           (pspec != NULL) ? g_param_spec_get_name (pspec) : "(all)");

  if (connection != NULL)
    connection_connect (self, connection, active_connection);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  /* Don’t bother working out what changed; just assume that it will probably
   * change our scheduling. */
  g_signal_emit_by_name (self, "connection-details-changed", active_connection_id);
}

static void
active_connection_state_changed_cb (NMActiveConnection *active_connection,
                                    guint               new_state,
                                    guint               reason,
                                    gpointer            user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);
  NMConnection *connection = NM_CONNECTION (nm_active_connection_get_connection (active_connection));

  const gchar *active_connection_id = NULL;
  active_connection_id = nm_active_connection_get_id (active_connection);

  g_debug ("%s: Active connection ‘%s’ state changed to %u (reason: %u).",
           G_STRFUNC, active_connection_id, new_state, reason);

  if (connection != NULL)
    connection_connect (self, connection, active_connection);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  g_signal_emit_by_name (self, "connection-details-changed", active_connection_id);
}

static void
device_added_cb (NMClient *client,
                 NMDevice *device,
                 gpointer  user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  g_debug ("%s: Adding device ‘%s’.", G_STRFUNC, nm_device_get_iface (device));
  device_connect (self, device);
}

static void
device_removed_cb (NMClient *client,
                   NMDevice *device,
                   gpointer  user_data)
{
  MwsConnectionMonitorNm *self = MWS_CONNECTION_MONITOR_NM (user_data);

  g_debug ("%s: Removing device ‘%s’.", G_STRFUNC, nm_device_get_iface (device));
  device_disconnect (self, device);
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
connection_changed_cb (NMConnection *connection,
                       gpointer      user_data)
{
  ConnectionChangedData *data = user_data;

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
