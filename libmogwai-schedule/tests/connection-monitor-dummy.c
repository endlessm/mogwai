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
#include <libmogwai-schedule/tests/connection-monitor-dummy.h>


static void mws_connection_monitor_dummy_connection_monitor_init (MwsConnectionMonitorInterface *iface);

static const gchar * const *mws_connection_monitor_dummy_get_connection_ids     (MwsConnectionMonitor *monitor);
static gboolean             mws_connection_monitor_dummy_get_connection_details (MwsConnectionMonitor *monitor,
                                                                                 const gchar          *id,
                                                                                 MwsConnectionDetails *out_details);

/**
 * MwsConnectionMonitorDummy:
 *
 * An implementation of the #MwsConnectionMonitor interface which always returns
 * empty or %FALSE results. To be used for testing only.
 *
 * Since: 0.1.0
 */
struct _MwsConnectionMonitorDummy
{
  GObject parent;
};

G_DEFINE_TYPE_WITH_CODE (MwsConnectionMonitorDummy, mws_connection_monitor_dummy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_CONNECTION_MONITOR,
                                                mws_connection_monitor_dummy_connection_monitor_init))
static void
mws_connection_monitor_dummy_class_init (MwsConnectionMonitorDummyClass *klass)
{
  /* Nothing here. */
}

static void
mws_connection_monitor_dummy_connection_monitor_init (MwsConnectionMonitorInterface *iface)
{
  iface->get_connection_ids = mws_connection_monitor_dummy_get_connection_ids;
  iface->get_connection_details = mws_connection_monitor_dummy_get_connection_details;
}

static void
mws_connection_monitor_dummy_init (MwsConnectionMonitorDummy *self)
{
  /* Nothing here. */
}

static const gchar * const connection_ids[] = { NULL, };

static const gchar * const *
mws_connection_monitor_dummy_get_connection_ids (MwsConnectionMonitor *monitor)
{
  return connection_ids;
}

static gboolean
mws_connection_monitor_dummy_get_connection_details (MwsConnectionMonitor *monitor,
                                                     const gchar          *id,
                                                     MwsConnectionDetails *out_details)
{
  return FALSE;
}

/**
 * mws_connection_monitor_dummy_new:
 *
 * Create a #MwsConnectionMonitorDummy object.
 *
 * Returns: (transfer full): a new #MwsConnectionMonitorDummy
 * Since: 0.1.0
 */
MwsConnectionMonitorDummy *
mws_connection_monitor_dummy_new (void)
{
  return g_object_new (MWS_TYPE_CONNECTION_MONITOR_DUMMY, NULL);
}
