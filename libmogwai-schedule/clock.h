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

#define MWS_TYPE_CLOCK mws_clock_get_type ()
G_DECLARE_INTERFACE (MwsClock, mws_clock, MWS, CLOCK, GObject)

/**
 * MwsClockInterface:
 * @g_iface: parent interface
 * @get_now_local: (not nullable): Get the current time, in the local timezone.
 *    This method must be implemented.
 * @add_alarm: (not nullable): Add a callback to be invoked when @alarm_time is
 *    reached. If  @alarm_time is now or in the past, the callback must be
 *    invoked immediately. This method must be implemented.
 * @remove_alarm: (not nullable): Remove a pending alarm using the ID returned
 *    by @add_alarm. If the alarm callback has already been invoked, this should
 *    emit a critical warning. This method must be implemented.
 *
 * An interface which provides wall clock timing and alarm functions to callers.
 * The standard implementation is #MwsClockSystem, which uses the system clock.
 * Other implementations may be available, which use mock data for testing.
 *
 * All virtual methods in this interface are mandatory to implement if the
 * interface is implemented.
 *
 * Since: 0.1.0
 */
struct _MwsClockInterface
{
  GTypeInterface g_iface;

  GDateTime *(*get_now_local) (MwsClock       *clock);
  guint      (*add_alarm)     (MwsClock       *clock,
                               GDateTime      *alarm_time,
                               GSourceFunc     alarm_func,
                               gpointer        user_data,
                               GDestroyNotify  destroy_func);
  void       (*remove_alarm)  (MwsClock       *clock,
                               guint           id);
};

GDateTime *mws_clock_get_now_local (MwsClock       *self);
guint      mws_clock_add_alarm     (MwsClock       *self,
                                    GDateTime      *alarm_time,
                                    GSourceFunc     alarm_func,
                                    gpointer        user_data,
                                    GDestroyNotify  destroy_func);
void       mws_clock_remove_alarm  (MwsClock       *self,
                                    guint           id);

G_END_DECLS
