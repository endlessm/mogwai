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


/**
 * mws_metered_combine_pessimistic:
 * @a: a metered status
 * @b: another metered status
 *
 * Combine two #MwsMetered values pessimistically, returning the one
 * which more conservatively estimates the metered status of a connection. For
 * example, if the two values were %MWS_METERED_GUESS_NO and
 * %MWS_METERED_GUESS_YES, the returned value would be %MWS_METERED_GUESS_YES.
 *
 * The return value is guaranteed to either be @a or @b.
 *
 * Returns: pessimistic choice between @a and @b
 * Since: 0.1.0
 */
MwsMetered
mws_metered_combine_pessimistic (MwsMetered a,
                                 MwsMetered b)
{
  if (a == b)
    return a;
  if (a == MWS_METERED_UNKNOWN)
    return b;
  if (b == MWS_METERED_UNKNOWN)
    return a;

  if (a == MWS_METERED_YES || b == MWS_METERED_YES)
    return MWS_METERED_YES;
  if (a == MWS_METERED_GUESS_YES || b == MWS_METERED_GUESS_YES)
    return MWS_METERED_GUESS_YES;
  if (a == MWS_METERED_GUESS_NO || b == MWS_METERED_GUESS_NO)
    return MWS_METERED_GUESS_NO;

  return MWS_METERED_NO;
}

/**
 * mws_metered_to_string:
 * @metered: a metered status
 *
 * Return a string form of the @metered status. This is intended for use in
 * debug output, and is not translated, stable, or user-friendly.
 *
 * Returns: metered status string
 * Since: 0.1.0
 */
const gchar *
mws_metered_to_string (MwsMetered metered)
{
  switch (metered)
    {
    case MWS_METERED_UNKNOWN:
      return "unknown";
    case MWS_METERED_YES:
      return "yes";
    case MWS_METERED_NO:
      return "no";
    case MWS_METERED_GUESS_YES:
      return "guess-yes";
    case MWS_METERED_GUESS_NO:
      return "guess-no";
    default:
      g_assert_not_reached ();
    }
}

/**
 * mws_connection_details_clear:
 * @details: (transfer full): a #MwsConnectionDetails
 *
 * Free any allocated memory in the given @details instance (but don’t free the
 * @details itself), and reset the instance to neutral default values.
 *
 * This should be used with stack-allocated #MwsConnectionDetails.
 *
 * Since: 0.1.0
 */
void
mws_connection_details_clear (MwsConnectionDetails *details)
{
  details->metered = MWS_METERED_UNKNOWN;
  details->download_when_metered = FALSE;
  g_clear_object (&details->tariff);
}

/**
 * MwsConnectionMonitor:
 *
 * A connection monitor is an abstraction over the OS’s network interface,
 * making the set of active network connections, and some of their details,
 * available to the scheduler to use in scheduling decisions.
 *
 * The default implementation for production use is #MwsConnectionMonitorNm,
 * which uses NetworkManager to get connection information. Unit tests will use
 * a different implementation, allowing them to provide fake data to the
 * scheduler easily.
 *
 * Each implementation of #MwsConnectionMonitor can define its own format for
 * IDs, but all IDs must be non-%NULL, non-empty, and valid UTF-8.
 *
 * Since: 0.1.0
 */

G_DEFINE_INTERFACE (MwsConnectionMonitor, mws_connection_monitor, G_TYPE_OBJECT)

static void
mws_connection_monitor_default_init (MwsConnectionMonitorInterface *iface)
{
  /**
   * MwsConnectionMonitor::connections-changed:
   * @self: a #MwsConnectionMonitor
   * @added_connections: (element-type utf8) (nullable): potentially empty or
   *     %NULL array of connection IDs which have been added (a %NULL array is
   *     equivalent to an empty one)
   * @removed_connections: (element-type utf8) (nullable): potentially empty or
   *     %NULL array of connection IDs which have been removed (a %NULL array is
   *     equivalent to an empty one)
   *
   * Emitted when the set of active connections has changed. The details of
   * new or changed connections can be queried using
   * mws_connection_monitor_get_connection_details().
   *
   * If a connection is listed in one of the two arrays, it will not be listed
   * in the other. There will be one connection listed in at least one of the
   * arrays.
   *
   * Since: 0.1.0
   */
  g_signal_new ("connections-changed", G_TYPE_FROM_INTERFACE (iface),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 2,
                G_TYPE_PTR_ARRAY,
                G_TYPE_PTR_ARRAY);

  /**
   * MwsConnectionMonitor::connection-details-changed:
   * @self: a #MwsConnectionMonitor
   * @connection_id: ID of the connection whose details have changed
   *
   * Emitted when the details of a connection have changed, to prompt
   * subscribers to re-query the connection’s details using
   * mws_connection_monitor_get_connection_details().
   *
   * There is no guarantee that the connection details have actually changed if
   * this signal is emitted.
   *
   * Since: 0.1.0
   */
  g_signal_new ("connection-details-changed", G_TYPE_FROM_INTERFACE (iface),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 1,
                G_TYPE_STRING);
}

/**
 * mws_connection_monitor_get_connection_ids:
 * @self: a #MwsConnectionMonitor
 *
 * Get the IDs of the currently active network connections on this system.
 * The returned array may be empty, but is guaranteed to be non-%NULL.
 *
 * Returns: (transfer none) (array zero-terminated=1): IDs of the active
 *    connections (may be empty)
 * Since: 0.1.0
 */
const gchar * const *
mws_connection_monitor_get_connection_ids (MwsConnectionMonitor *self)
{
  g_return_val_if_fail (MWS_IS_CONNECTION_MONITOR (self), NULL);

  MwsConnectionMonitorInterface *iface = MWS_CONNECTION_MONITOR_GET_IFACE (self);
  g_assert (iface->get_connection_ids != NULL);
  const gchar * const *ids = iface->get_connection_ids (self);

  g_return_val_if_fail (ids != NULL, NULL);

  return ids;
}

/**
 * mws_connection_monitor_get_connection_details:
 * @self: a #MwsConnectionMonitor
 * @id: ID of the connection to get details for
 * @out_details: (out caller-allocates) (optional): pointer to a
 *    #MwsConnectionDetails to fill in, or %NULL to not receive the information
 *
 * Get the current details of the active connection with the given @id. If
 * @out_details is %NULL, this can be used to check whether a given @id is
 * valid.
 *
 * The populated @out_details may contain heap-allocated memory, ownership of
 * which is transferred to the caller. Call mws_connection_details_clear() to
 * free it afterwards.
 *
 * Returns: %TRUE if @id is valid and @out_details has been filled (if provided);
 *    %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_connection_monitor_get_connection_details (MwsConnectionMonitor *self,
                                               const gchar          *id,
                                               MwsConnectionDetails *out_details)
{
  g_return_val_if_fail (MWS_IS_CONNECTION_MONITOR (self), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  MwsConnectionMonitorInterface *iface = MWS_CONNECTION_MONITOR_GET_IFACE (self);
  g_assert (iface->get_connection_details != NULL);
  return iface->get_connection_details (self, id, out_details);
}
