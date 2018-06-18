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
#include <libmogwai-schedule/tests/clock-dummy.h>


static void mws_clock_dummy_clock_init (MwsClockInterface *iface);

static void mws_clock_dummy_finalize (GObject *obj);

static GDateTime *mws_clock_dummy_get_now_local (MwsClock       *clock);
static guint      mws_clock_dummy_add_alarm     (MwsClock       *clock,
                                                 GDateTime      *alarm_time,
                                                 GSourceFunc     alarm_func,
                                                 gpointer        user_data,
                                                 GDestroyNotify  destroy_func);
static void       mws_clock_dummy_remove_alarm  (MwsClock       *clock,
                                                 guint           id);

typedef struct
{
  GDateTime *alarm_time;  /* (owned) */
  GSourceFunc alarm_func;  /* (nullable) */
  gpointer user_data;  /* (nullable) */
  GDestroyNotify destroy_func;  /* (nullable) */
} AlarmData;

static void
alarm_data_free (AlarmData *data)
{
  /* FIXME: In order to be able to steal elements from the alarms array, we need
   * to gracefully ignore NULL entries. */
  if (data == NULL)
    return;

  g_clear_pointer (&data->alarm_time, g_date_time_unref);
  if (data->destroy_func != NULL && data->user_data != NULL)
    data->destroy_func (data->user_data);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (AlarmData, alarm_data_free)

static gint
alarm_data_compare (const AlarmData *a,
                    const AlarmData *b)
{
  if (!g_date_time_equal (a->alarm_time, b->alarm_time))
    return g_date_time_compare (a->alarm_time, b->alarm_time);

  /* Tie break using the pointer values, so the sort is stable. */
  if (a == b)
    return 0;
  else if (a < b)
    return -1;
  else
    return 1;
}

static void
alarm_data_invoke (const AlarmData *data)
{
  if (data->alarm_func != NULL)
    data->alarm_func (data->user_data);
}

/**
 * MwsClockDummy:
 *
 * An implementation of the #MwsClock interface which is not tied to any
 * real-world clock. Its time transitions are entirely programmatically driven
 * by calling mws_clock_dummy_set_time() and mws_clock_dummy_set_time_zone(),
 * which will typically be done by a test harness. Its internal clock will not
 * progress automatically at all.
 *
 * This is intended for testing code which uses the #MwsClock interface.
 *
 * The time in the dummy clock may move forwards or backwards and its timezone
 * can be changed (which results in the #MwsClock::offset-changed signal being
 * emitted).
 *
 * The clock starts at 2000-01-01T00:00:00Z.
 *
 * Since: 0.1.0
 */
struct _MwsClockDummy
{
  GObject parent;

  /* Main context from construction time. */
  GMainContext *context;  /* (owned) */
  GSource *add_alarm_source;  /* (owned) (nullable) */

  /* The current time. This only changes when the test harness explicitly
   * advances it. It’s always maintained in @tz. */
  GDateTime *now;  /* (owned) */
  GTimeZone *tz;  /* (owned) */

  /* Array of pending alarms, sorted by increasing date/time. */
  GPtrArray *alarms;  /* (owned) (element-type AlarmData) */
};

G_DEFINE_TYPE_WITH_CODE (MwsClockDummy, mws_clock_dummy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_CLOCK,
                                                mws_clock_dummy_clock_init))
static void
mws_clock_dummy_class_init (MwsClockDummyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mws_clock_dummy_finalize;
}

static void
mws_clock_dummy_clock_init (MwsClockInterface *iface)
{
  iface->get_now_local = mws_clock_dummy_get_now_local;
  iface->add_alarm = mws_clock_dummy_add_alarm;
  iface->remove_alarm = mws_clock_dummy_remove_alarm;
}

static void
mws_clock_dummy_init (MwsClockDummy *self)
{
  /* Start at the year 2000 UTC, so things are reproducible. We can’t start at
   * the year 1, as then the initial timezone transition might not be possible. */
  self->tz = g_time_zone_new_utc ();
  self->now = g_date_time_new (self->tz, 2000, 1, 1, 0, 0, 0);
  g_assert (self->now != NULL);

  self->context = g_main_context_ref_thread_default ();

  self->alarms = g_ptr_array_new_with_free_func ((GDestroyNotify) alarm_data_free);
}

static void
mws_clock_dummy_finalize (GObject *obj)
{
  MwsClockDummy *self = MWS_CLOCK_DUMMY (obj);

  g_clear_pointer (&self->alarms, g_ptr_array_unref);
  g_clear_pointer (&self->tz, g_time_zone_unref);
  g_clear_pointer (&self->now, g_date_time_unref);

  if (self->add_alarm_source != NULL)
    {
      g_source_destroy (self->add_alarm_source);
      g_clear_pointer (&self->add_alarm_source, g_source_unref);
    }
  g_clear_pointer (&self->context, g_main_context_unref);

  G_OBJECT_CLASS (mws_clock_dummy_parent_class)->finalize (obj);
}

static GDateTime *
mws_clock_dummy_get_now_local (MwsClock *clock)
{
  MwsClockDummy *self = MWS_CLOCK_DUMMY (clock);
  return g_date_time_ref (self->now);
}

static gint
alarm_data_compare_cb (gconstpointer a,
                       gconstpointer b)
{
  return alarm_data_compare (*((const AlarmData **) a),
                             *((const AlarmData **) b));
}

static gboolean
add_alarm_cb (gpointer user_data)
{
  MwsClockDummy *self = MWS_CLOCK_DUMMY (user_data);

  g_autoptr(GDateTime) now = mws_clock_dummy_get_now_local (MWS_CLOCK (self));
  mws_clock_dummy_set_time (self, now);
  g_clear_pointer (&self->add_alarm_source, g_source_unref);

  return G_SOURCE_REMOVE;
}

static guint
mws_clock_dummy_add_alarm (MwsClock       *clock,
                           GDateTime      *alarm_time,
                           GSourceFunc     alarm_func,
                           gpointer        user_data,
                           GDestroyNotify  destroy_func)
{
  MwsClockDummy *self = MWS_CLOCK_DUMMY (clock);

  g_autoptr(AlarmData) data = g_new0 (AlarmData, 1);
  data->alarm_time = g_date_time_ref (alarm_time);
  data->alarm_func = alarm_func;
  data->user_data = user_data;
  data->destroy_func = destroy_func;

  guint id = GPOINTER_TO_UINT (data);
  g_ptr_array_add (self->alarms, g_steal_pointer (&data));
  g_ptr_array_sort (self->alarms, alarm_data_compare_cb);

  /* Ensure the alarm is triggered if it’s in the past, but don’t trigger it
   * immediately to avoid re-entrancy issues. */
  if (self->add_alarm_source == NULL)
    {
      self->add_alarm_source = g_idle_source_new ();
      g_source_set_callback (self->add_alarm_source, add_alarm_cb, self, NULL);
      g_source_attach (self->add_alarm_source, self->context);
    }

  return id;
}

static void
mws_clock_dummy_remove_alarm (MwsClock *clock,
                              guint     id)
{
  MwsClockDummy *self = MWS_CLOCK_DUMMY (clock);
  GSource *source = GUINT_TO_POINTER (id);

  /* The source will be destroyed by the free function set up on the array. */
  g_return_if_fail (g_ptr_array_remove_fast (self->alarms, source));
}

/**
 * mws_clock_dummy_new:
 *
 * Create a #MwsClockDummy object which gets wall clock time from the dummy
 * clock.
 *
 * Returns: (transfer full): a new #MwsClockDummy
 * Since: 0.1.0
 */
MwsClockDummy *
mws_clock_dummy_new (void)
{
  return g_object_new (MWS_TYPE_CLOCK_DUMMY, NULL);
}

/**
 * mws_clock_dummy_set_time:
 * @self: a #MwsClockDummy
 * @now: new time for the clock
 *
 * Set the clock to consider ‘now’ to be @now. This will result in any alarms
 * whose trigger times are earlier than or equal to @now being triggered. Any
 * new alarms added from alarm callbacks are also invoked if their trigger times
 * meet the same criterion.
 *
 * The time zone for @now is not used to change the time zone for the clock;
 * @now is converted to the current time zone in use by the clock. To change the
 * clock’s time zone, call mws_clock_dummy_set_time_zone().
 *
 * Since: 0.1.0
 */
void
mws_clock_dummy_set_time (MwsClockDummy *self,
                          GDateTime     *now)
{
  g_return_if_fail (MWS_IS_CLOCK_DUMMY (self));
  g_return_if_fail (now != NULL);

  g_autofree gchar *now_str = g_date_time_format (now, "%FT%T%:::z");
  g_debug ("%s: Setting time to %s; %u alarms to check",
           G_STRFUNC, now_str, self->alarms->len);

  /* Trigger all the alarms before the new @now (inclusive).
   * Since other methods on #MwsClockDummy may be called by the user function
   * in alarm_data_invoke(), which may modify the @self->alarms array, we need
   * to be careful to re-read the array on each loop iteration, and to remove
   * invoked alarms individually. */
  while (self->alarms->len > 0)
    {
      g_autoptr(AlarmData) owned_data = NULL;
      const AlarmData *data = g_ptr_array_index (self->alarms, 0);

      g_autofree gchar *alarm_time_str = g_date_time_format (data->alarm_time, "%FT%T%:::z");
      g_debug ("%s: Comparing alarm %s to now %s",
               G_STRFUNC, alarm_time_str, now_str);

      if (g_date_time_compare (data->alarm_time, now) > 0)
        break;

      /* Steal the alarm from the array.
       * FIXME: Use g_ptr_array_steal_index() when we can depend on a suitable GLib version. */
      owned_data = g_steal_pointer (&self->alarms->pdata[0]);
      g_ptr_array_remove_index (self->alarms, 0);

      /* Set the current time to what the alarm expects. */
      g_date_time_unref (self->now);
      self->now = g_date_time_to_timezone (owned_data->alarm_time, self->tz);

      alarm_data_invoke (owned_data);
    }

  /* Convert the final time to our local timezone. */
  g_date_time_unref (self->now);
  self->now = g_date_time_to_timezone (now, self->tz);
}

/**
 * mws_clock_dummy_set_time_zone:
 * @self: a #MwsClockDummy
 * @tz: new time zone for the clock
 *
 * Set the clock’s time zone to @tz, and convert its current ‘now’ time to be
 * the same instant as before, but in @tz.
 *
 * If the time zone has changed, this results in the #MwsClock::offset-changed
 * signal being emitted.
 *
 * Since: 0.1.0
 */
void
mws_clock_dummy_set_time_zone (MwsClockDummy *self,
                               GTimeZone     *tz)
{
  g_return_if_fail (MWS_IS_CLOCK_DUMMY (self));
  g_return_if_fail (tz != NULL);

  /* FIXME: Do we need a g_time_zone_equal() function to compare these? This
   * is a terrible way of comparing them. */
  if (tz == self->tz)
    return;

  g_debug ("%s: Setting time zone to %s (%p)",
           G_STRFUNC, g_time_zone_get_abbreviation (tz, 0), tz);

  g_autoptr(GDateTime) now_new_tz = g_date_time_to_timezone (self->now, tz);
  g_return_if_fail (now_new_tz != NULL);
  g_date_time_unref (self->now);
  self->now = g_steal_pointer (&now_new_tz);

  g_time_zone_unref (self->tz);
  self->tz = g_time_zone_ref (tz);

  g_signal_emit_by_name (self, "offset-changed");
}

/**
 * mws_clock_dummy_get_next_alarm_time:
 * @self: a #MwsClockDummy
 *
 * Get the time when the next alarm will be triggered.
 *
 * Returns: time of the next alarm, or %NULL if there are currently no alarms
 *    scheduled
 * Since: 0.1.0
 */
GDateTime *
mws_clock_dummy_get_next_alarm_time (MwsClockDummy *self)
{
  g_return_val_if_fail (MWS_IS_CLOCK_DUMMY (self), NULL);

  if (self->alarms->len == 0)
    return NULL;

  const AlarmData *data = g_ptr_array_index (self->alarms, 0);
  return data->alarm_time;
}

/**
 * mws_clock_dummy_next_alarm:
 * @self: a #MwsClockDummy
 *
 * Advance the clock to the time of the next alarm (as determined by
 * mws_clock_dummy_get_next_alarm_time()) using mws_clock_dummy_set_time().
 *
 * If there are no alarms scheduled, this function is a no-op and returns
 * %FALSE.
 *
 * Returns: %TRUE if there was a next alarm, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_clock_dummy_next_alarm (MwsClockDummy *self)
{
  g_return_val_if_fail (MWS_IS_CLOCK_DUMMY (self), FALSE);

  GDateTime *next_alarm_time = mws_clock_dummy_get_next_alarm_time (self);
  if (next_alarm_time == NULL)
    return FALSE;

  mws_clock_dummy_set_time (self, next_alarm_time);

  return TRUE;
}
