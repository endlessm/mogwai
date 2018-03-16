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
#include <libmogwai-schedule/peer-manager.h>

G_BEGIN_DECLS

#define MWS_TYPE_PEER_MANAGER_DBUS mws_peer_manager_dbus_get_type ()
G_DECLARE_FINAL_TYPE (MwsPeerManagerDBus, mws_peer_manager_dbus, MWS, PEER_MANAGER_DBUS, GObject)

MwsPeerManagerDBus *mws_peer_manager_dbus_new (GDBusConnection *connection);

G_END_DECLS
