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
#include <libmogwai-schedule/clock.h>

G_BEGIN_DECLS

#define MWS_TYPE_CLOCK_DUMMY mws_clock_dummy_get_type ()
G_DECLARE_FINAL_TYPE (MwsClockDummy, mws_clock_dummy, MWS, CLOCK_DUMMY, GObject)

MwsClockDummy *mws_clock_dummy_new (void);

void       mws_clock_dummy_set_time            (MwsClockDummy *self,
                                                GDateTime     *now);
void       mws_clock_dummy_set_time_zone       (MwsClockDummy *self,
                                                GTimeZone     *tz);
GDateTime *mws_clock_dummy_get_next_alarm_time (MwsClockDummy *self);
gboolean   mws_clock_dummy_next_alarm          (MwsClockDummy *self);

G_END_DECLS
