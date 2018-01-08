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

G_BEGIN_DECLS

/**
 * mws_schedule_entry_id_is_valid:
 * @entry_id: a potentially valid entry ID
 *
 * Validate @entry_id to check it’s not a disallowed ID.
 *
 * Returns: %TRUE if @entry_id is valid, %FALSE otherwise
 * Since: 0.1.0
 */
static inline gboolean
mws_schedule_entry_id_is_valid (const gchar *entry_id)
{
  return (entry_id != NULL && *entry_id != '\0');
}

#define MWS_TYPE_SCHEDULE_ENTRY mws_schedule_entry_get_type ()
G_DECLARE_FINAL_TYPE (MwsScheduleEntry, mws_schedule_entry, MWS, SCHEDULE_ENTRY, GObject)

MwsScheduleEntry   *mws_schedule_entry_new              (const gchar       *owner);
MwsScheduleEntry   *mws_schedule_entry_new_from_variant (const gchar       *owner,
                                                         GVariant          *parameters,
                                                         GError           **error);

const gchar        *mws_schedule_entry_get_id           (MwsScheduleEntry  *self);
const gchar        *mws_schedule_entry_get_owner        (MwsScheduleEntry  *self);

guint32             mws_schedule_entry_get_priority     (MwsScheduleEntry  *self);
void                mws_schedule_entry_set_priority     (MwsScheduleEntry  *self,
                                                         guint32            priority);
gboolean            mws_schedule_entry_get_resumable    (MwsScheduleEntry  *self);
void                mws_schedule_entry_set_resumable    (MwsScheduleEntry  *self,
                                                         gboolean           resumable);

G_END_DECLS
