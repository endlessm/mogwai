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
#include <glib-object.h>
#include <libmogwai-schedule/connection-monitor.h>
#include <libmogwai-schedule/peer-manager.h>
#include <libmogwai-schedule/schedule-entry.h>

G_BEGIN_DECLS

/**
 * MwsSchedulerError:
 * @MWS_SCHEDULER_ERROR_FULL: There are enough schedule entries in the scheduler
 *    and it has hit its resource limits.
 * @MWS_SCHEDULER_ERROR_IDENTIFYING_PEER: A peer which was requesting a schedule
 *    entry to be added could not be identified.
 * @MWS_SCHEDULER_ERROR_INVALID_PARAMETERS: A schedule entry could not be
 *    created due to having invalid parameters.
 *
 * Errors which can be returned by #MwsScheduler.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWS_SCHEDULER_ERROR_FULL = 0,
  MWS_SCHEDULER_ERROR_IDENTIFYING_PEER,
  MWS_SCHEDULER_ERROR_INVALID_PARAMETERS,
} MwsSchedulerError;
#define MWS_SCHEDULER_N_ERRORS (MWS_SCHEDULER_ERROR_INVALID_PARAMETERS + 1)

GQuark mws_scheduler_error_quark (void);
#define MWS_SCHEDULER_ERROR mws_scheduler_error_quark ()

#define MWS_TYPE_SCHEDULER mws_scheduler_get_type ()
G_DECLARE_FINAL_TYPE (MwsScheduler, mws_scheduler, MWS, SCHEDULER, GObject)

MwsScheduler     *mws_scheduler_new             (MwsConnectionMonitor *connection_monitor,
                                                 MwsPeerManager       *peer_manager);

MwsPeerManager   *mws_scheduler_get_peer_manager (MwsScheduler     *self);

gboolean          mws_scheduler_update_entries  (MwsScheduler      *self,
                                                 GPtrArray         *added,
                                                 GPtrArray         *removed,
                                                 GError           **error);
gboolean          mws_scheduler_remove_entries_for_owner (MwsScheduler  *self,
                                                          const gchar   *owner,
                                                          GError       **error);

MwsScheduleEntry *mws_scheduler_get_entry       (MwsScheduler      *self,
                                                 const gchar       *entry_id);
GHashTable       *mws_scheduler_get_entries     (MwsScheduler      *self);

gboolean          mws_scheduler_is_entry_active (MwsScheduler      *self,
                                                 MwsScheduleEntry  *entry);

void              mws_scheduler_reschedule      (MwsScheduler *self);

G_END_DECLS
