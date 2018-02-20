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
#include <libmogwai-schedule/connection-monitor.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

#define MWS_TYPE_CONNECTION_MONITOR_NM mws_connection_monitor_nm_get_type ()
G_DECLARE_FINAL_TYPE (MwsConnectionMonitorNm, mws_connection_monitor_nm, MWS, CONNECTION_MONITOR_NM, GObject)

MwsConnectionMonitorNm *mws_connection_monitor_nm_new_from_client (NMClient  *client,
                                                                   GError   **error);
void                    mws_connection_monitor_nm_new_async       (GCancellable         *cancellable,
                                                                   GAsyncReadyCallback   callback,
                                                                   gpointer              user_data);
MwsConnectionMonitorNm *mws_connection_monitor_nm_new_finish      (GAsyncResult         *result,
                                                                   GError              **error);

G_END_DECLS
