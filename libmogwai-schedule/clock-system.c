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
#include <libmogwai-schedule/clock-system.h>


/**
 * SECTION:clock-system
 * @short_description: Implementation of #MwsClock using the system wall clock
 * @stability: Unstable
 * @include: libmogwai-schedule/clock-system.h
 *
 * #MwsClockSystem is the standard implementation of #MwsClock, which uses the
 * system wall clock to provide time and alarms. Internally, it uses
 * g_date_time_new_now_local() to provide time, and
 * g_timeout_source_new_seconds() to provide alarms. It adds #GSources to the
 * thread-default main context from when the #MwsClockSystem was constructed.
 * That main context must be running in order for alarm callbacks to be invoked.
 *
 * FIXME: Currently, this does not support detecting when the system timezone
 * or underlying RTC clock changes, and hence it never emits the
 * #MwsClock::offset-changed signal. See
 * https://phabricator.endlessm.com/T21845.
 *
 * Since: 0.1.0
 */

static void mws_clock_system_clock_init (MwsClockInterface *iface);

static void mws_clock_system_finalize (GObject *obj);

static GDateTime *mws_clock_system_get_now_local (MwsClock       *clock);
static guint      mws_clock_system_add_alarm     (MwsClock       *clock,
                                                  GDateTime      *alarm_time,
                                                  GSourceFunc     alarm_func,
                                                  gpointer        user_data,
                                                  GDestroyNotify  destroy_func);
static void       mws_clock_system_remove_alarm  (MwsClock       *clock,
                                                  guint           id);

/**
 * MwsClockSystem:
 *
 * Implementation of #MwsClock which uses the system wall clock.
 *
 * Since: 0.1.0
 */
struct _MwsClockSystem
{
  GObject parent;

  GMainContext *context;  /* (owned) */
  GPtrArray *alarms;  /* (owned) (element-type GSource) */
};

G_DEFINE_TYPE_WITH_CODE (MwsClockSystem, mws_clock_system, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_CLOCK,
                                                mws_clock_system_clock_init))
static void
mws_clock_system_class_init (MwsClockSystemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mws_clock_system_finalize;
}

static void
mws_clock_system_clock_init (MwsClockInterface *iface)
{
  iface->get_now_local = mws_clock_system_get_now_local;
  iface->add_alarm = mws_clock_system_add_alarm;
  iface->remove_alarm = mws_clock_system_remove_alarm;
}

static void
destroy_and_unref_source (GSource *source)
{
  g_source_destroy (source);
  g_source_unref (source);
}

static void
mws_clock_system_init (MwsClockSystem *self)
{
  self->context = g_main_context_ref_thread_default ();
  self->alarms = g_ptr_array_new_with_free_func ((GDestroyNotify) destroy_and_unref_source);
}

static void
mws_clock_system_finalize (GObject *obj)
{
  MwsClockSystem *self = MWS_CLOCK_SYSTEM (obj);

  g_clear_pointer (&self->alarms, g_ptr_array_unref);
  g_clear_pointer (&self->context, g_main_context_unref);

  G_OBJECT_CLASS (mws_clock_system_parent_class)->finalize (obj);
}

static GDateTime *
mws_clock_system_get_now_local (MwsClock *clock)
{
  return g_date_time_new_now_local ();
}

static guint
mws_clock_system_add_alarm (MwsClock       *clock,
                            GDateTime      *alarm_time,
                            GSourceFunc     alarm_func,
                            gpointer        user_data,
                            GDestroyNotify  destroy_func)
{
  MwsClockSystem *self = MWS_CLOCK_SYSTEM (clock);

  g_autoptr(GDateTime) now = mws_clock_system_get_now_local (clock);

  /* If the @alarm_time is in the past, invoke the callback on the next main
   * context iteration. */
  GTimeSpan interval = g_date_time_difference (alarm_time, now);
  if (interval < 0)
    interval = 0;

  g_autoptr(GSource) source = g_timeout_source_new_seconds (interval / G_USEC_PER_SEC);
  g_source_set_callback (source, alarm_func, user_data, destroy_func);
  g_source_attach (source, self->context);

  guint id = g_source_get_id (source);
  g_ptr_array_add (self->alarms, g_steal_pointer (&source));

  g_autofree gchar *alarm_time_str = NULL;
  alarm_time_str = g_date_time_format (alarm_time, "%FT%T%:::z");
  g_debug ("%s: Setting alarm %u for %s (in %" G_GUINT64_FORMAT " seconds)",
           G_STRFUNC, id, alarm_time_str, (guint64) interval / G_USEC_PER_SEC);

  return id;
}

static void
mws_clock_system_remove_alarm (MwsClock *clock,
                               guint     id)
{
  MwsClockSystem *self = MWS_CLOCK_SYSTEM (clock);
  GSource *source = g_main_context_find_source_by_id (self->context, id);

  g_debug ("%s: Removing alarm %u", G_STRFUNC, id);

  /* The source will be destroyed by the free function set up on the array. */
  g_return_if_fail (g_ptr_array_remove_fast (self->alarms, source));
}

/**
 * mws_clock_system_new:
 *
 * Create a #MwsClockSystem object which gets wall clock time from the system
 * clock.
 *
 * Returns: (transfer full): a new #MwsClockSystem
 * Since: 0.1.0
 */
MwsClockSystem *
mws_clock_system_new (void)
{
  return g_object_new (MWS_TYPE_CLOCK_SYSTEM, NULL);
}
