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


/**
 * MwsPeerManager:
 *
 * A peer manager is an abstraction over the management of peers on a D-Bus
 * connection, monitoring when they disappear, and allowing querying and caching
 * of their credentials.
 *
 * Currently, the only credential stored is the path to the peer’s executable,
 * which can be used to identify that peer. In future, other credentials may be
 * added (and the API will change accordingly).
 *
 * The default implementation for production use is #MwsPeerManagerDBus,
 * which uses the D-Bus daemon to get credentials. Unit tests will use a
 * different implementation, allowing them to provide fake data to the scheduler
 * easily.
 *
 * Since: 0.1.0
 */

G_DEFINE_INTERFACE (MwsPeerManager, mws_peer_manager, G_TYPE_OBJECT)

static void
mws_peer_manager_default_init (MwsPeerManagerInterface *iface)
{
  /**
   * MwsPeerManager::peer-vanished:
   * @self: a #MwsPeerManager
   * @name: unique name of the D-Bus peer which vanished
   *
   * Emitted when a peer disappears off the bus. The peer’s unique name will be
   * given as @name.
   *
   * Since: 0.1.0
   */
  g_signal_new ("peer-vanished", G_TYPE_FROM_INTERFACE (iface),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 1,
                G_TYPE_STRING);
}

/**
 * mws_peer_manager_ensure_peer_credentials_async:
 * @self: a #MwsPeerManager
 * @sender: D-Bus unique name for the peer
 *
 * Ensure the credentials for a peer are in the peer manager, querying them from
 * the D-Bus daemon if needed. Also start watching the @sender, so that if it
 * disappears from the bus, a #MwsPeerManager::peer-vanished signal will be
 * emitted.
 *
 * Since: 0.1.0
 */
void
mws_peer_manager_ensure_peer_credentials_async (MwsPeerManager      *self,
                                                const gchar         *sender,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data)
{
  g_return_if_fail (MWS_IS_PEER_MANAGER (self));
  g_return_if_fail (g_dbus_is_unique_name (sender));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  MwsPeerManagerInterface *iface = MWS_PEER_MANAGER_GET_IFACE (self);
  g_assert (iface->ensure_peer_credentials_async != NULL);
  iface->ensure_peer_credentials_async (self, sender, cancellable, callback, user_data);
}

/**
 * mws_peer_manager_ensure_peer_credentials_finish:
 * @self: a #MwsPeerManager
 * @result: asynchronous operation result
 * @error: return location for a #GError
 *
 * Finish ensuring the credentials for a peer are in the peer manager. See
 * mws_peer_manager_ensure_peer_credentials_async().
 *
 * Returns: (transfer full): path to the executable for the peer,
 *    or %NULL on error
 * Since: 0.1.0
 */
gchar *
mws_peer_manager_ensure_peer_credentials_finish (MwsPeerManager  *self,
                                                 GAsyncResult    *result,
                                                 GError         **error)
{
  g_return_val_if_fail (MWS_IS_PEER_MANAGER (self), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  MwsPeerManagerInterface *iface = MWS_PEER_MANAGER_GET_IFACE (self);
  g_assert (iface->ensure_peer_credentials_finish != NULL);

  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *sender_path = iface->ensure_peer_credentials_finish (self, result,
                                                                         &local_error);
  g_return_val_if_fail ((sender_path == NULL) == (local_error != NULL), NULL);

  if (local_error != NULL)
    g_propagate_error (error, g_steal_pointer (&local_error));
  return g_steal_pointer (&sender_path);
}

/**
 * mws_peer_manager_get_peer_credentials:
 * @self: a #MwsPeerManager
 * @sender: D-Bus unique name for the peer
 *
 * Get the credentials for the given peer. If no credentials are in the cache
 * for @sender, %NULL will be returned.
 *
 * Returns: (nullable): path to the executable for the peer, or %NULL if it’s
 *    unknown
 * Since: 0.1.0
 */
const gchar *
mws_peer_manager_get_peer_credentials (MwsPeerManager *self,
                                       const gchar    *sender)
{
  g_return_val_if_fail (MWS_IS_PEER_MANAGER (self), NULL);
  g_return_val_if_fail (g_dbus_is_unique_name (sender), NULL);

  MwsPeerManagerInterface *iface = MWS_PEER_MANAGER_GET_IFACE (self);
  g_assert (iface->get_peer_credentials != NULL);

  return iface->get_peer_credentials (self, sender);
}
