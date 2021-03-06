/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018, 2019 Endless Mobile, Inc.
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

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>
#include <libmogwai-schedule-client/schedule-entry.h>

G_BEGIN_DECLS

/**
 * MwscSchedulerError:
 * @MWSC_SCHEDULER_ERROR_INVALIDATED: The scheduler has disappeared on the bus,
 *    due to the service disappearing.
 * @MWSC_SCHEDULER_ERROR_FULL: There are enough schedule entries in the
 *    scheduler and it has hit its resource limits.
 * @MWSC_SCHEDULER_ERROR_IDENTIFYING_PEER: The scheduler could not determine
 *    required details about this peer process.
 *
 * Errors which can be returned by #MwscScheduler.
 *
 * Since: 0.1.0
 */
typedef enum
{
  /* These are local-only: */
  MWSC_SCHEDULER_ERROR_INVALIDATED = 0,
  /* These map to errors from the daemon: */
  MWSC_SCHEDULER_ERROR_FULL,
  MWSC_SCHEDULER_ERROR_IDENTIFYING_PEER,
} MwscSchedulerError;
#define MWSC_SCHEDULER_N_ERRORS (MWSC_SCHEDULER_ERROR_IDENTIFYING_PEER - MWSC_SCHEDULER_ERROR_INVALIDATED)

GQuark mwsc_scheduler_error_quark (void);
#define MWSC_SCHEDULER_ERROR mwsc_scheduler_error_quark ()

#define MWSC_TYPE_SCHEDULER mwsc_scheduler_get_type ()
G_DECLARE_FINAL_TYPE (MwscScheduler, mwsc_scheduler, MWSC, SCHEDULER, GObject)

MwscScheduler     *mwsc_scheduler_new             (GCancellable         *cancellable,
                                                   GError              **error);
void               mwsc_scheduler_new_async       (GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
MwscScheduler     *mwsc_scheduler_new_finish      (GAsyncResult         *result,
                                                   GError              **error);

MwscScheduler     *mwsc_scheduler_new_full        (GDBusConnection      *connection,
                                                   const gchar          *name,
                                                   const gchar          *object_path,
                                                   GCancellable         *cancellable,
                                                   GError              **error);
void               mwsc_scheduler_new_full_async  (GDBusConnection      *connection,
                                                   const gchar          *name,
                                                   const gchar          *object_path,
                                                   GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
MwscScheduler     *mwsc_scheduler_new_full_finish (GAsyncResult         *result,
                                                   GError              **error);

MwscScheduler     *mwsc_scheduler_new_from_proxy  (GDBusProxy           *proxy,
                                                   GError              **error);

MwscScheduleEntry *mwsc_scheduler_schedule        (MwscScheduler        *self,
                                                   GVariant             *parameters,
                                                   GCancellable         *cancellable,
                                                   GError              **error);
void               mwsc_scheduler_schedule_async  (MwscScheduler        *self,
                                                   GVariant             *parameters,
                                                   GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
MwscScheduleEntry *mwsc_scheduler_schedule_finish (MwscScheduler        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);

GPtrArray         *mwsc_scheduler_schedule_entries        (MwscScheduler        *self,
                                                           GPtrArray            *parameters,
                                                           GCancellable         *cancellable,
                                                           GError              **error);
void               mwsc_scheduler_schedule_entries_async  (MwscScheduler        *self,
                                                           GPtrArray            *parameters,
                                                           GCancellable         *cancellable,
                                                           GAsyncReadyCallback   callback,
                                                           gpointer              user_data);
GPtrArray         *mwsc_scheduler_schedule_entries_finish (MwscScheduler        *self,
                                                           GAsyncResult         *result,
                                                           GError              **error);

gboolean           mwsc_scheduler_hold           (MwscScheduler        *self,
                                                  const gchar          *reason,
                                                  GCancellable         *cancellable,
                                                  GError              **error);
void               mwsc_scheduler_hold_async     (MwscScheduler        *self,
                                                  const gchar          *reason,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);
gboolean           mwsc_scheduler_hold_finish    (MwscScheduler        *self,
                                                  GAsyncResult         *result,
                                                  GError              **error);

gboolean           mwsc_scheduler_release        (MwscScheduler        *self,
                                                  GCancellable         *cancellable,
                                                  GError              **error);
void               mwsc_scheduler_release_async  (MwscScheduler        *self,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);
gboolean           mwsc_scheduler_release_finish (MwscScheduler        *self,
                                                  GAsyncResult         *result,
                                                  GError              **error);

gboolean           mwsc_scheduler_get_allow_downloads (MwscScheduler *self);

G_END_DECLS
