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

G_BEGIN_DECLS

/**
 * MwscScheduleEntryError:
 * @MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED: The schedule entry has disappeared
 *    on the bus, due to the service disappearing or the entry being explicitly
 *    removed.
 * @MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY: The entry didn’t exist or this
 *    process doesn’t have permission to access it.
 *
 * Errors which can be returned by #MwscScheduleEntry.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED = 0,
  MWSC_SCHEDULE_ENTRY_ERROR_UNKNOWN_ENTRY,
} MwscScheduleEntryError;
#define MWSC_SCHEDULE_ENTRY_N_ERRORS (MWSC_SCHEDULE_ENTRY_ERROR_INVALIDATED + 1)

GQuark mwsc_schedule_entry_error_quark (void);
#define MWSC_SCHEDULE_ENTRY_ERROR mwsc_schedule_entry_error_quark ()

#define MWSC_TYPE_SCHEDULE_ENTRY mwsc_schedule_entry_get_type ()
G_DECLARE_FINAL_TYPE (MwscScheduleEntry, mwsc_schedule_entry, MWSC, SCHEDULE_ENTRY, GObject)

void                mwsc_schedule_entry_new_full_async   (GDBusConnection      *connection,
                                                          const gchar          *name,
                                                          const gchar          *object_path,
                                                          GCancellable         *cancellable,
                                                          GAsyncReadyCallback   callback,
                                                          gpointer              user_data);
MwscScheduleEntry  *mwsc_schedule_entry_new_full_finish  (GAsyncResult         *result,
                                                          GError              **error);

MwscScheduleEntry  *mwsc_schedule_entry_new_from_proxy   (GDBusProxy           *proxy,
                                                          GError              **error);

const gchar        *mwsc_schedule_entry_get_id           (MwscScheduleEntry    *self);

gboolean            mwsc_schedule_entry_get_download_now (MwscScheduleEntry    *self);

guint32             mwsc_schedule_entry_get_priority     (MwscScheduleEntry    *self);
void                mwsc_schedule_entry_set_priority     (MwscScheduleEntry    *self,
                                                          guint32               priority);
gboolean            mwsc_schedule_entry_get_resumable    (MwscScheduleEntry    *self);
void                mwsc_schedule_entry_set_resumable    (MwscScheduleEntry    *self,
                                                          gboolean              resumable);

void     mwsc_schedule_entry_send_properties_async  (MwscScheduleEntry    *self,
                                                     GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean mwsc_schedule_entry_send_properties_finish (MwscScheduleEntry    *self,
                                                     GAsyncResult         *result,
                                                     GError              **error);

void     mwsc_schedule_entry_remove_async  (MwscScheduleEntry    *self,
                                            GCancellable         *cancellable,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data);
gboolean mwsc_schedule_entry_remove_finish (MwscScheduleEntry    *entry,
                                            GAsyncResult         *result,
                                            GError              **error);

G_END_DECLS
