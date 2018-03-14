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

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MWS_TYPE_PEER_MANAGER mws_peer_manager_get_type ()
G_DECLARE_INTERFACE (MwsPeerManager, mws_peer_manager, MWS, PEER_MANAGER, GObject)

/**
 * MwsPeerManagerInterface:
 * @g_iface: parent interface
 * @ensure_peer_credentials_async: Query for the credentials of the given peer,
 *    and ensure they are in the peer manager’s cache.
 * @ensure_peer_credentials_finish: Finish an asynchronous query operation
 *    started with @ensure_peer_credentials_async.
 * @get_peer_credentials: Get credentials for a peer out of the peer manager’s
 *    cache. If the peer is not known to the manager, return %NULL.
 *
 * An interface which exposes peers for the scheduler (typically, D-Bus clients
 * which are adding schedule entries to the scheduler) and allows querying of
 * their credentials, and notification of when they disappear.
 *
 * All virtual methods in this interface are mandatory to implement if the
 * interface is implemented.
 *
 * Since: 0.1.0
 */
struct _MwsPeerManagerInterface
{
  GTypeInterface g_iface;

  void         (*ensure_peer_credentials_async)  (MwsPeerManager       *manager,
                                                  const gchar          *sender,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);
  gchar       *(*ensure_peer_credentials_finish) (MwsPeerManager       *manager,
                                                  GAsyncResult         *result,
                                                  GError              **error);

  const gchar *(*get_peer_credentials)           (MwsPeerManager       *manager,
                                                  const gchar          *sender);
};

void         mws_peer_manager_ensure_peer_credentials_async  (MwsPeerManager       *self,
                                                              const gchar          *sender,
                                                              GCancellable         *cancellable,
                                                              GAsyncReadyCallback  callback,
                                                              gpointer              user_data);
gchar       *mws_peer_manager_ensure_peer_credentials_finish (MwsPeerManager       *self,
                                                              GAsyncResult         *result,
                                                              GError              **error);

const gchar *mws_peer_manager_get_peer_credentials           (MwsPeerManager       *self,
                                                              const gchar          *sender);

G_END_DECLS
