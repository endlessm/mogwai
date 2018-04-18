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
 * Declaration of the com.endlessm.DownloadManager1.Scheduler D-Bus interface.
 *
 * FIXME: Ideally, there would be a gdbus-codegen mode to generate just this
 * interface info, because writing it out in C is horrific.
 * See: https://bugzilla.gnome.org/show_bug.cgi?id=795304
 */

static const GDBusArgInfo scheduler_interface_schedule_arg_properties =
{
  -1,  /* ref count */
  (gchar *) "properties",
  (gchar *) "a{sv}",
  NULL
};

static const GDBusArgInfo scheduler_interface_schedule_arg_entry =
{
  -1,  /* ref count */
  (gchar *) "entry",
  (gchar *) "o",
  NULL
};

static const GDBusArgInfo *scheduler_interface_schedule_in_args[] =
{
  &scheduler_interface_schedule_arg_properties,
  NULL,
};
static const GDBusArgInfo *scheduler_interface_schedule_out_args[] =
{
  &scheduler_interface_schedule_arg_entry,
  NULL,
};
static const GDBusMethodInfo scheduler_interface_schedule =
{
  -1,  /* ref count */
  (gchar *) "Schedule",
  (GDBusArgInfo **) scheduler_interface_schedule_in_args,
  (GDBusArgInfo **) scheduler_interface_schedule_out_args,
  NULL,  /* annotations */
};

static const GDBusArgInfo scheduler_interface_schedule_entries_arg_properties =
{
  -1,  /* ref count */
  (gchar *) "properties",
  (gchar *) "aa{sv}",
  NULL
};

static const GDBusArgInfo scheduler_interface_schedule_entries_arg_entries =
{
  -1,  /* ref count */
  (gchar *) "entry_array",
  (gchar *) "ao",
  NULL
};

static const GDBusArgInfo *scheduler_interface_schedule_entries_in_args[] =
{
  &scheduler_interface_schedule_entries_arg_properties,
  NULL,
};
static const GDBusArgInfo *scheduler_interface_schedule_entries_out_args[] =
{
  &scheduler_interface_schedule_entries_arg_entries,
  NULL,
};
static const GDBusMethodInfo scheduler_interface_schedule_entries =
{
  -1,  /* ref count */
  (gchar *) "ScheduleEntries",
  (GDBusArgInfo **) scheduler_interface_schedule_entries_in_args,
  (GDBusArgInfo **) scheduler_interface_schedule_entries_out_args,
  NULL,  /* annotations */
};

static const GDBusMethodInfo *scheduler_interface_methods[] =
{
  &scheduler_interface_schedule,
  &scheduler_interface_schedule_entries,
  NULL,
};

static const GDBusPropertyInfo scheduler_interface_entry_count =
{
  -1, /* ref count */
  "EntryCount",
  "u",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* annotations */
};

static const GDBusPropertyInfo scheduler_interface_active_entry_count =
{
  -1, /* ref count */
  "ActiveEntryCount",
  "u",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* annotations */
};

static const GDBusPropertyInfo *scheduler_interface_properties[] =
{
  &scheduler_interface_entry_count,
  &scheduler_interface_active_entry_count,
  NULL,
};

static const GDBusInterfaceInfo scheduler_interface =
{
  -1,  /* ref count */
  (gchar *) "com.endlessm.DownloadManager1.Scheduler",
  (GDBusMethodInfo **) scheduler_interface_methods,
  NULL,  /* no signals */
  (GDBusPropertyInfo **) scheduler_interface_properties,
  NULL,  /* no annotations */
};

static const gchar *scheduler_errors[] =
{
  "com.endlessm.DownloadManager1.Scheduler.Error.Full",
  "com.endlessm.DownloadManager1.Scheduler.Error.IdentifyingPeer",
};

G_END_DECLS
