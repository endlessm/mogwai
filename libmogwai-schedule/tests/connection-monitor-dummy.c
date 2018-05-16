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


static void
connection_details_copy (const MwsConnectionDetails *details,
                         MwsConnectionDetails       *copy)
{
  copy->metered = details->metered;
  copy->allow_downloads_when_metered = details->allow_downloads_when_metered;
  copy->allow_downloads = details->allow_downloads;
  copy->tariff = (details->tariff != NULL) ? g_object_ref (details->tariff) : NULL;
}

static void
connection_details_free (MwsConnectionDetails *details)
{
  mws_connection_details_clear (details);
  g_free (details);
}

static void mws_connection_monitor_dummy_connection_monitor_init (MwsConnectionMonitorInterface *iface);

static void mws_connection_monitor_dummy_finalize (GObject *object);

static const gchar * const *mws_connection_monitor_dummy_get_connection_ids     (MwsConnectionMonitor *monitor);
static gboolean             mws_connection_monitor_dummy_get_connection_details (MwsConnectionMonitor *monitor,
                                                                                 const gchar          *id,
                                                                                 MwsConnectionDetails *out_details);

/**
 * MwsConnectionMonitorDummy:
 *
 * An implementation of the #MwsConnectionMonitor interface which returns dummy
 * results provided using mws_connection_monitor_dummy_update_connections() and
 * mws_connection_monitor_dummy_update_connection(). To be used for testing
 * only.
 *
 * Since: 0.1.0
 */
struct _MwsConnectionMonitorDummy
{
  GObject parent;

  GHashTable *connections;  /* (owned) (element-type utf8 MwsConnectionDetails) */
  const gchar **cached_connection_ids;  /* (owned) (nullable) */
};

G_DEFINE_TYPE_WITH_CODE (MwsConnectionMonitorDummy, mws_connection_monitor_dummy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_CONNECTION_MONITOR,
                                                mws_connection_monitor_dummy_connection_monitor_init))
static void
mws_connection_monitor_dummy_class_init (MwsConnectionMonitorDummyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mws_connection_monitor_dummy_finalize;
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
  self->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                             (GDestroyNotify) connection_details_free);
  self->cached_connection_ids = NULL;
}

static void
mws_connection_monitor_dummy_finalize (GObject *object)
{
  MwsConnectionMonitorDummy *self = MWS_CONNECTION_MONITOR_DUMMY (object);

  g_clear_pointer (&self->connections, g_hash_table_unref);
  g_clear_pointer (&self->cached_connection_ids, g_free);

  G_OBJECT_CLASS (mws_connection_monitor_dummy_parent_class)->finalize (object);
}

static const gchar * const *
mws_connection_monitor_dummy_get_connection_ids (MwsConnectionMonitor *monitor)
{
  MwsConnectionMonitorDummy *self = MWS_CONNECTION_MONITOR_DUMMY (monitor);

  if (self->cached_connection_ids == NULL)
    self->cached_connection_ids =
        (const gchar **) g_hash_table_get_keys_as_array (self->connections, NULL);

  return self->cached_connection_ids;
}

static gboolean
mws_connection_monitor_dummy_get_connection_details (MwsConnectionMonitor *monitor,
                                                     const gchar          *id,
                                                     MwsConnectionDetails *out_details)
{
  MwsConnectionMonitorDummy *self = MWS_CONNECTION_MONITOR_DUMMY (monitor);

  const MwsConnectionDetails *found_details = g_hash_table_lookup (self->connections, id);

  if (found_details != NULL)
    {
      connection_details_copy (found_details, out_details);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
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

/**
 * mws_connection_monitor_dummy_update_connections:
 * @self: a #MwsConnectionMonitorDummy
 * @added: (element-type utf8 MwsConnectionDetails) (transfer none) (nullable):
 *    mock connections to add to the connection monitor, mapping connection ID
 *    to connection details, or %NULL to not add any
 * @removed: (element-type utf8) (transfer none) (nullable): connection IDs of
 *    mock connections to remove from the connection monitor, or %NULL to not
 *    remove any
 *
 * Update the set of mock connections exported by the connection monitor,
 * removing those in @removed and then adding those in @added. It is an error to
 * try to remove a connection ID which is not currently in the connection
 * monitor; similarly, it is an error to try to add a connection ID which
 * already exists in the connection monitor.
 *
 * If @added or @removed were non-empty,
 * #MwsConnectionMonitor::connections-changed is emitted after all changes have
 * been made.
 *
 * Since: 0.1.0
 */
void
mws_connection_monitor_dummy_update_connections (MwsConnectionMonitorDummy *self,
                                                 GHashTable                *added,
                                                 GPtrArray                 *removed)
{
  g_return_if_fail (MWS_IS_CONNECTION_MONITOR_DUMMY (self));

  g_autoptr(GPtrArray) actually_added = g_ptr_array_new_with_free_func (NULL);
  g_autoptr(GPtrArray) actually_removed = g_ptr_array_new_with_free_func (NULL);

  for (gsize i = 0; removed != NULL && i < removed->len; i++)
    {
      const gchar *id = g_ptr_array_index (removed, i);
      g_assert (g_hash_table_remove (self->connections, id));
      g_ptr_array_add (actually_removed, id);
    }

  if (added != NULL)
    {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init (&iter, added);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          const gchar *id = key;
          const MwsConnectionDetails *details = value;

          /* Note: We don’t do autoptrs properly here, because #MwsConnectionDetails
           * is supposed to be stack allocated. */
          MwsConnectionDetails *details_copy = g_new0 (MwsConnectionDetails, 1);
          connection_details_copy (details, details_copy);

          g_assert (g_hash_table_insert (self->connections, g_strdup (id), g_steal_pointer (&details_copy)));
          g_ptr_array_add (actually_added, id);
        }
    }

  if (actually_added->len > 0 || actually_removed->len > 0)
    {
      g_clear_pointer (&self->cached_connection_ids, g_free);
      g_signal_emit_by_name (self, "connections-changed", actually_added, actually_removed);
    }
}

/**
 * mws_connection_monitor_dummy_update_connection:
 * @self: a #MwsConnectionMonitorDummy
 * @connection_id: ID of the connection to update
 * @details: new details for the connection
 *
 * Update the details of the existing connection identified by @connection_id
 * in the connection monitor.
 *
 * It is an error to call this with a @connection_id which does not exist in the
 * connection monitor.
 *
 * The #MwsConnectionMonitor::connection-details-changed signal will be emitted
 * once the changes have been made in the connection monitor.
 *
 * Since: 0.1.0
 */
void
mws_connection_monitor_dummy_update_connection (MwsConnectionMonitorDummy  *self,
                                                const gchar                *connection_id,
                                                const MwsConnectionDetails *details)
{
  g_return_if_fail (MWS_IS_CONNECTION_MONITOR_DUMMY (self));
  g_return_if_fail (connection_id != NULL);
  g_return_if_fail (details != NULL);

  g_assert (g_hash_table_lookup (self->connections, connection_id) != NULL);

  /* Note: We don’t do autoptrs properly here, because #MwsConnectionDetails
   * is supposed to be stack allocated. */
  MwsConnectionDetails *details_copy = g_new0 (MwsConnectionDetails, 1);
  connection_details_copy (details, details_copy);

  g_assert (!g_hash_table_replace (self->connections, g_strdup (connection_id),
                                   g_steal_pointer (&details_copy)));

  g_clear_pointer (&self->cached_connection_ids, g_free);
  g_signal_emit_by_name (self, "connection-details-changed", connection_id);
}
