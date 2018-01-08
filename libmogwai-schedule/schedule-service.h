/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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
#include <gio/gio.h>
#include <libmogwai-schedule/scheduler.h>

G_BEGIN_DECLS

#define MWS_TYPE_SCHEDULE_SERVICE mws_schedule_service_get_type ()
G_DECLARE_FINAL_TYPE (MwsScheduleService, mws_schedule_service, MWS,
                      SCHEDULE_SERVICE, GObject)

MwsScheduleService *mws_schedule_service_new (GDBusConnection *connection,
                                              const gchar     *object_path,
                                              MwsScheduler    *scheduler);

gboolean mws_schedule_service_register   (MwsScheduleService  *self,
                                          GError             **error);
void     mws_schedule_service_unregister (MwsScheduleService  *self);

G_END_DECLS
