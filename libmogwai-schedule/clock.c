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
#include <libmogwai-schedule/clock.h>


/**
 * SECTION:clock
 * @short_description: Interface to wall clock timing and alarm functionality
 * @stability: Unstable
 * @include: libmogwai-schedule/clock.h
 *
 * #MwsClock is an interface to wall clock timing and alarm functionality, and
 * is provided as an abstraction over the standard system clock functions. By
 * abstracting timing, unit tests can test timing-specific behaviour without
 * having to use multi-hour sleeps or being subject to race conditions.
 *
 * The typical runtime implementation of this interface is #MwsClockSystem,
 * which uses the system clock.
 *
 * A typical test implementation of this interface is #MwsClockDummy, which
 * allows its time and timezone to be controlled by the calling test harness.
 *
 * Since: 0.1.0
 */

/**
 * MwsClock:
 *
 * Interface providing wall clock timing and alarm functionality.
 *
 * Since: 0.1.0
 */

G_DEFINE_INTERFACE (MwsClock, mws_clock, G_TYPE_OBJECT)

static void
mws_clock_default_init (MwsClockInterface *iface)
{
  /**
   * MwsClock::offset-changed:
   * @self: a #MwsClock
   *
   * Emitted when the clock offset (timezone, or underlying RTC time) changes,
   * such that any stored offsets from wall clock time need to be recalculated.
   *
   * Since: 0.1.0
   */
  g_signal_new ("offset-changed", G_TYPE_FROM_INTERFACE (iface),
                G_SIGNAL_RUN_LAST,
                0, NULL, NULL, NULL,
                G_TYPE_NONE, 0);
}

/**
 * mws_clock_get_now_local:
 * @self: a #MwsClock
 *
 * Get the current time, in the time zone currently in use by the clock (which
 * will typically be the local time zone).
 *
 * Returns: (transfer full): current clock time
 * Since: 0.1.0
 */
GDateTime *
mws_clock_get_now_local (MwsClock *self)
{
  g_return_val_if_fail (MWS_IS_CLOCK (self), NULL);

  MwsClockInterface *iface = MWS_CLOCK_GET_IFACE (self);
  g_assert (iface->get_now_local != NULL);
  return iface->get_now_local (self);
}

/**
 * mws_clock_add_alarm:
 * @self: a #MwsClock
 * @alarm_time: when to trigger the alarm
 * @alarm_func: (nullable): function to invoke when @alarm_time is reached
 * @user_data: (nullable): data to pass to @alarm_func
 * @destroy_func: (nullable): destroy function for @user_data
 *
 * Add an alarm to the clock, which will invoke @alarm_func at the first
 * opportunity after the clock reaches @alarm_time.
 *
 * @user_data will be passed to @alarm_func, and @destroy_func will be used to
 * destroy @user_data afterwards (if both of them are non-%NULL).
 *
 * If @alarm_time is in the past, @alarm_func is guaranteed to be invoked at
 * the first opportunity after the call to mws_clock_add_alarm() returns.
 *
 * Returns: ID of the alarm, valid until the alarm is invoked or removed using
 *    mws_clock_remove_alarm()
 * Since: 0.1.0
 */
guint
mws_clock_add_alarm (MwsClock       *self,
                     GDateTime      *alarm_time,
                     GSourceFunc     alarm_func,
                     gpointer        user_data,
                     GDestroyNotify  destroy_func)
{
  g_return_val_if_fail (MWS_IS_CLOCK (self), 0);
  g_return_val_if_fail (alarm_time != NULL, 0);

  MwsClockInterface *iface = MWS_CLOCK_GET_IFACE (self);
  g_assert (iface->add_alarm != NULL);
  guint id = iface->add_alarm (self, alarm_time, alarm_func, user_data, destroy_func);
  g_assert (id != 0);
  return id;
}

/**
 * mws_clock_remove_alarm:
 * @self: a #MwsClock
 * @id: ID of the alarm to remove, as returned by mws_clock_add_alarm()
 *
 * Remove a pending alarm from the clock, using the ID returned by
 * mws_clock_add_alarm() when it was added. @id must be valid and must not be
 * removed more than once.
 *
 * Since: 0.1.0
 */
void
mws_clock_remove_alarm (MwsClock *self,
                        guint     id)
{
  g_return_if_fail (MWS_IS_CLOCK (self));
  g_return_if_fail (id != 0);

  MwsClockInterface *iface = MWS_CLOCK_GET_IFACE (self);
  g_assert (iface->remove_alarm != NULL);
  iface->remove_alarm (self, id);
}
