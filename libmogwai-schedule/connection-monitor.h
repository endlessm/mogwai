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
#include <libmogwai-tariff/tariff.h>

G_BEGIN_DECLS

/**
 * MwsMetered:
 * @MWS_METERED_UNKNOWN: Metered status is unknown.
 * @MWS_METERED_YES: Definitely metered.
 * @MWS_METERED_NO: Definitely not metered.
 * @MWS_METERED_GUESS_YES: Probably metered.
 * @MWS_METERED_GUESS_NO: Probably not metered.
 *
 * Enum describing whether a connection is metered (the user has to pay per unit
 * of traffic sent over it). This includes a coarse-grained measure of the
 * certainty in whether the connection is metered.
 *
 * This type is identical to #NMMetered, but without pulling in the
 * NetworkManager namespace.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWS_METERED_UNKNOWN = 0,
  MWS_METERED_YES,
  MWS_METERED_NO,
  MWS_METERED_GUESS_YES,
  MWS_METERED_GUESS_NO,
} MwsMetered;

MwsMetered   mws_metered_combine_pessimistic (MwsMetered a,
                                              MwsMetered b);
const gchar *mws_metered_to_string           (MwsMetered metered);

/**
 * MwsConnectionDetails:
 * @metered: Whether the connection is metered.
 * @download_when_metered: %TRUE to download even if the connection is metered,
 *    %FALSE otherwise
 * @tariff: (nullable) (owned): Tariff information for this connection.
 *
 * Information about the configuration and current state of a given connection.
 * This struct may be expanded in future to add new fields.
 *
 * Since: 0.1.0
 */
typedef struct
{
  MwsMetered metered;
  gboolean download_when_metered;
  MwtTariff *tariff;
} MwsConnectionDetails;

void mws_connection_details_clear (MwsConnectionDetails *details);

#define MWS_TYPE_CONNECTION_MONITOR mws_connection_monitor_get_type ()
G_DECLARE_INTERFACE (MwsConnectionMonitor, mws_connection_monitor, MWS, CONNECTION_MONITOR, GObject)

/**
 * MwsConnectionMonitorInterface:
 * @g_iface: parent interface
 * @get_connection_ids: Get the IDs of the currently active network connections.
 *    The format of an ID is defined by the interface implementation, but it
 *    must be non-%NULL, non-empty, and valid UTF-8.
 * @get_connection_details: Get the current details of the given connection,
 *    returning them in @out_details. If the connection is found, %TRUE is
 *    returned; otherwise, @out_details is undefined and %FALSE is returned.
 *
 * An interface which exposes the currently active network connections and their
 * properties which are useful to the #MwsScheduler for working out scheduling
 * of downloads.
 *
 * All virtual methods in this interface are mandatory to implement if the
 * interface is implemented.
 *
 * Since: 0.1.0
 */
struct _MwsConnectionMonitorInterface
{
  GTypeInterface g_iface;

  const gchar * const *(*get_connection_ids)     (MwsConnectionMonitor *monitor);
  gboolean             (*get_connection_details) (MwsConnectionMonitor *monitor,
                                                  const gchar          *id,
                                                  MwsConnectionDetails *out_details);
};

const gchar * const *mws_connection_monitor_get_connection_ids     (MwsConnectionMonitor *self);
gboolean             mws_connection_monitor_get_connection_details (MwsConnectionMonitor *self,
                                                                    const gchar          *id,
                                                                    MwsConnectionDetails *out_details);

G_END_DECLS
