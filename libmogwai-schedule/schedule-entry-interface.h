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

#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Declaration of the com.endlessm.DownloadManager1.ScheduleEntry D-Bus interface.
 */

static const GDBusMethodInfo schedule_entry_interface_remove =
{
  -1,  /* ref count */
  (gchar *) "Remove",
  NULL,  /* no in args */
  NULL,  /* no out args */
  NULL,  /* annotations */
};

static const GDBusMethodInfo *schedule_entry_interface_methods[] =
{
  &schedule_entry_interface_remove,
  NULL,
};

static const GDBusPropertyInfo schedule_entry_interface_download_now =
{
  -1,  /* ref count */
  (gchar *) "DownloadNow",
  (gchar *) "b",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* no annotations */
};
static const GDBusPropertyInfo schedule_entry_interface_priority =
{
  -1,  /* ref count */
  (gchar *) "Priority",
  (gchar *) "u",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE | G_DBUS_PROPERTY_INFO_FLAGS_WRITABLE,
  NULL,  /* no annotations */
};
static const GDBusPropertyInfo schedule_entry_interface_resumable =
{
  -1,  /* ref count */
  (gchar *) "Resumable",
  (gchar *) "b",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE | G_DBUS_PROPERTY_INFO_FLAGS_WRITABLE,
  NULL,  /* no annotations */
};

static const GDBusPropertyInfo *schedule_entry_interface_properties[] =
{
  &schedule_entry_interface_download_now,
  &schedule_entry_interface_priority,
  &schedule_entry_interface_resumable,
  NULL,
};

static const GDBusInterfaceInfo schedule_entry_interface =
{
  -1,  /* ref count */
  (gchar *) "com.endlessm.DownloadManager1.ScheduleEntry",
  (GDBusMethodInfo **) schedule_entry_interface_methods,
  NULL,  /* no signals */
  (GDBusPropertyInfo **) schedule_entry_interface_properties,
  NULL,  /* no annotations */
};

G_END_DECLS
