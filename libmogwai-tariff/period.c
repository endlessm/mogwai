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

#include <glib/gi18n-lib.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <libmogwai-tariff/enums.h>
#include <libmogwai-tariff/period.h>


G_DEFINE_QUARK (MwtPeriodError, mwt_period_error)

static void mwt_period_constructed  (GObject      *object);
static void mwt_period_dispose      (GObject      *object);
static void mwt_period_get_property (GObject      *object,
                                     guint         property_id,
                                     GValue       *value,
                                     GParamSpec   *pspec);
static void mwt_period_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);

/**
 * MwtPeriod:
 *
 * A representation of a period in a tariff where the tariff properties are
 * constant (for example, a single capacity limit applies to the whole period).
 *
 * It has a start and end time, and properties which control how it repeats (if
 * at all). The start time is inclusive, but the end time is exclusive (which
 * makes handling of leap seconds at the end of a period easier).
 *
 * Repeats take leap years and timezone changes into account. For example, if a
 * period spans 01:00 to 06:00 on 31st January, and repeats every month, a
 * recurrence will happen on 28th February (or 29th February on a leap year),
 * on 31st March, 30th April, etc.
 *
 * If a period spans 01:00 to 02:00 on a normal day, and a DST transition
 * happens where the clocks go forward by 1 hour at 01:00 on a certain day, any
 * recurrence of the period on that day will be skipped. Recurrences on days
 * after the DST transition will happen at 01:00 to 02:00 in the new timezone.
 *
 * For a DST transition where the clocks go backward by 1 hour at 02:00 on a
 * certain day, the time span 01:00–02:00 will happen twice. Any recurrence of a
 * period which spans 01:00 to 02:00 will happen on the first occurrence of the
 * time span, and will not repeat during the second occurrence.
 *
 * The #MwtPeriod class is immutable once loaded or constructed.
 *
 * Since: 0.1.0
 */
struct _MwtPeriod
{
  GObject parent;

  GDateTime *start;  /* (owned) */
  GDateTime *end;  /* (owned) */

  MwtPeriodRepeatType repeat_type;
  guint repeat_period;

  guint64 capacity_limit;
};

typedef enum
{
  PROP_START = 1,
  PROP_END,
  PROP_REPEAT_TYPE,
  PROP_REPEAT_PERIOD,
  PROP_CAPACITY_LIMIT,
} MwtPeriodProperty;

G_DEFINE_TYPE (MwtPeriod, mwt_period, G_TYPE_OBJECT)

static void
mwt_period_class_init (MwtPeriodClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_CAPACITY_LIMIT + 1] = { NULL, };

  object_class->constructed = mwt_period_constructed;
  object_class->dispose = mwt_period_dispose;
  object_class->get_property = mwt_period_get_property;
  object_class->set_property = mwt_period_set_property;

  /**
   * MwtPeriod:start:
   *
   * Date/Time when the period starts for the first time (inclusive).
   *
   * Since: 0.1.0
   */
  props[PROP_START] =
      g_param_spec_boxed ("start", "Start Date/Time",
                          "Date/Time when the period starts for the first time.",
                          G_TYPE_DATE_TIME,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * MwtPeriod:end:
   *
   * Date/Time when the period ends for the first time (exclusive). Repeats of
   * this period will occur after this date/time.
   *
   * Since: 0.1.0
   */
  props[PROP_END] =
      g_param_spec_boxed ("end", "End Date/Time",
                          "Date/Time when the period ends for the first time.",
                          G_TYPE_DATE_TIME,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * MwtPeriod:repeat-type:
   *
   * How this period repeats (if at all).
   *
   * A more generalised way of repeating periods may be added in future,
   * following the example of iCalendar’s `RRULE`.
   *
   * Since: 0.1.0
   */
  props[PROP_REPEAT_TYPE] =
      g_param_spec_enum ("repeat-type", "Repeat Type",
                         "How this period repeats (if at all).",
                         MWT_TYPE_PERIOD_REPEAT_TYPE,
                         MWT_PERIOD_REPEAT_NONE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * MwtPeriod:repeat-period:
   *
   * The period between repeats of this period. For example, if
   * #MwtPeriod:repeat-type was %MWT_PERIOD_REPEAT_HOUR and
   * #MwtPeriod:repeat-period was 6, this period would repeat once every 6
   * hours.
   *
   * Since: 0.1.0
   */
  props[PROP_REPEAT_PERIOD] =
      g_param_spec_uint ("repeat-period", "Repeat Period",
                         "The period between repeats of this period.",
                         0, G_MAXUINT, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * MwtPeriod:capacity-limit:
   *
   * Limit on the download capacity allowed during each repeat of this period,
   * in bytes. If this is zero, no downloading is allowed during any repeat of
   * this period. If it is %G_MAXUINT64, no limit is applied.
   *
   * The default is %G_MAXUINT64 (no limit).
   *
   * Since: 0.1.0
   */
  props[PROP_CAPACITY_LIMIT] =
      g_param_spec_uint64 ("capacity-limit", "Capacity Limit",
                           "Limit on the download capacity allowed during "
                           "each repeat of this period, in bytes.",
                           0, G_MAXUINT64, G_MAXUINT64,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
mwt_period_init (MwtPeriod *self)
{
  /* Nothing to do here. */
}

static void
mwt_period_constructed (GObject *object)
{
  MwtPeriod *self = MWT_PERIOD (object);

  G_OBJECT_CLASS (mwt_period_parent_class)->constructed (object);

  /* Validate properties. */
  g_assert (mwt_period_validate (self->start, self->end, self->repeat_type,
                                 self->repeat_period, NULL));
}

static void
mwt_period_dispose (GObject *object)
{
  MwtPeriod *self = MWT_PERIOD (object);

  g_clear_pointer (&self->start, g_date_time_unref);
  g_clear_pointer (&self->end, g_date_time_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwt_period_parent_class)->dispose (object);
}

static void
mwt_period_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MwtPeriod *self = MWT_PERIOD (object);

  switch ((MwtPeriodProperty) property_id)
    {
    case PROP_START:
      g_value_set_boxed (value, self->start);
      break;
    case PROP_END:
      g_value_set_boxed (value, self->end);
      break;
    case PROP_REPEAT_TYPE:
      g_value_set_enum (value, self->repeat_type);
      break;
    case PROP_REPEAT_PERIOD:
      g_value_set_uint (value, self->repeat_period);
      break;
    case PROP_CAPACITY_LIMIT:
      g_value_set_uint64 (value, self->capacity_limit);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mwt_period_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  MwtPeriod *self = MWT_PERIOD (object);

  switch ((MwtPeriodProperty) property_id)
    {
    case PROP_START:
      /* Construct only. */
      g_assert (self->start == NULL);
      self->start = g_value_dup_boxed (value);
      break;
    case PROP_END:
      /* Construct only. */
      g_assert (self->end == NULL);
      self->end = g_value_dup_boxed (value);
      break;
    case PROP_REPEAT_TYPE:
      /* Construct only. */
      g_assert (self->repeat_type == MWT_PERIOD_REPEAT_NONE);
      self->repeat_type = g_value_get_enum (value);
      break;
    case PROP_REPEAT_PERIOD:
      /* Construct only. */
      g_assert (self->repeat_period == 0);
      self->repeat_period = g_value_get_uint (value);
      break;
    case PROP_CAPACITY_LIMIT:
      /* Construct only. */
      g_assert (self->capacity_limit == 0);
      self->capacity_limit = g_value_get_uint64 (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * mwt_period_validate:
 * @start: (nullable): start date/time (see #MwtPeriod:start)
 * @end: (nullable): end date/time (see #MwtPeriod:end)
 * @repeat_type: repeat type (see #MwtPeriod:repeat-type)
 * @repeat_period: repeat period (see #MwtPeriod:repeat-period)
 * @error: return location for a #GError, or %NULL
 *
 * Validate the given #MwtPeriod properties, returning %MWT_PERIOD_ERROR_INVALID
 * if any of them are invalid. All inputs are allowed to the property arguments
 * (except @error): no inputs are a programmer error.
 *
 * It is guaranteed that if this function returns %TRUE for a given set of
 * inputs, mwt_period_new() will succeed for those inputs.
 *
 * Returns: %TRUE if valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_period_validate (GDateTime            *start,
                     GDateTime            *end,
                     MwtPeriodRepeatType   repeat_type,
                     guint                 repeat_period,
                     GError              **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (start == NULL ||
      end == NULL ||
      g_date_time_compare (start, end) >= 0)
    {
      g_set_error_literal (error, MWT_PERIOD_ERROR, MWT_PERIOD_ERROR_INVALID,
                           _("Invalid start/end times for period."));
      return FALSE;
    }

  if (!((gint) repeat_type >= MWT_PERIOD_REPEAT_NONE &&
        (gint) repeat_type <= MWT_PERIOD_REPEAT_YEAR))
    {
      g_set_error_literal (error, MWT_PERIOD_ERROR, MWT_PERIOD_ERROR_INVALID,
                           _("Invalid repeat type for period."));
      return FALSE;
    }

  if ((repeat_type == MWT_PERIOD_REPEAT_NONE) != (repeat_period == 0))
    {
      g_set_error_literal (error, MWT_PERIOD_ERROR, MWT_PERIOD_ERROR_INVALID,
                           _("Invalid repeat properties for period."));
      return FALSE;
    }

  return TRUE;
}

/**
 * mwt_period_new:
 * @start: start date/time (see #MwtPeriod:start)
 * @end: end date/time (see #MwtPeriod:end)
 * @repeat_type: repeat type (see #MwtPeriod:repeat-type)
 * @repeat_period: repeat period (see #MwtPeriod:repeat-period)
 * @...: additional properties, given as property name followed by value, with
 *    a %NULL terminator
 *
 * Create a #MwtPeriod object with the given properties. The @start, @end,
 * @repeat_type and @repeat_period properties are required. The varargs can be
 * used to specify the limits which apply to this period and which differ from
 * their default values. The varargs are specified in the same format as used
 * by g_object_new(), and the list must be %NULL terminated.
 *
 * Note that any 64-bit varargs must be cast to the correct type (for example,
 * using G_GUINT64_CONSTANT()), or the wrong number of bytes will be put on
 * the varargs list on non-64-bit architectures.
 *
 * All inputs to this function must have been validated with
 * mwt_period_validate() first. It is a programmer error to provide invalid
 * inputs.
 *
 * Returns: (transfer full): a new #MwtPeriod
 * Since: 0.1.0
 */
MwtPeriod *
mwt_period_new (GDateTime            *start,
                GDateTime            *end,
                MwtPeriodRepeatType   repeat_type,
                guint                 repeat_period,
                const gchar          *first_property_name,
                ...)
{
  /* Leave property validation to constructed(). */

  /* FIXME: This is mildly horrific. Do something clever with
   * g_object_new_with_properties(). */
  guint64 capacity_limit = G_MAXUINT64;
  if (g_strcmp0 (first_property_name, "capacity-limit") == 0)
    {
      va_list args;
      va_start (args, first_property_name);
      capacity_limit = va_arg (args, guint64);
      va_end (args);
    }

  return g_object_new (MWT_TYPE_PERIOD,
                       "start", start,
                       "end", end,
                       "repeat-type", repeat_type,
                       "repeat-period", repeat_period,
                       "capacity-limit", capacity_limit,
                       NULL);
}

/**
 * mwt_period_get_start:
 * @self: a #MwtPeriod
 *
 * Get the value of #MwtPeriod:start.
 *
 * Returns: start date/time (inclusive)
 * Since: 0.1.0
 */
GDateTime *
mwt_period_get_start (MwtPeriod *self)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), NULL);

  return self->start;
}

/**
 * mwt_period_get_end:
 * @self: a #MwtPeriod
 *
 * Get the value of #MwtPeriod:end.
 *
 * Returns: end date/time (exclusive)
 * Since: 0.1.0
 */
GDateTime *
mwt_period_get_end (MwtPeriod *self)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), NULL);

  return self->end;
}

/**
 * mwt_period_get_repeat_type:
 * @self: a #MwtPeriod
 *
 * Get the value of #MwtPeriod:repeat-type.
 *
 * Returns: repeat type
 * Since: 0.1.0
 */
MwtPeriodRepeatType
mwt_period_get_repeat_type (MwtPeriod *self)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), MWT_PERIOD_REPEAT_NONE);

  g_assert (self->repeat_type != MWT_PERIOD_REPEAT_NONE ||
            self->repeat_period == 0);

  return self->repeat_type;
}

/**
 * mwt_period_get_repeat_period:
 * @self: a #MwtPeriod
 *
 * Get the value of #MwtPeriod:repeat-period.
 *
 * Returns: repeat period
 * Since: 0.1.0
 */
guint
mwt_period_get_repeat_period (MwtPeriod *self)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), 0);

  g_assert (self->repeat_period != 0 ||
            self->repeat_type == MWT_PERIOD_REPEAT_NONE);

  return self->repeat_period;
}

/**
 * mwt_period_get_capacity_limit:
 * @self: a #MwtPeriod
 *
 * Get the value of #MwtPeriod:capacity-limit.
 *
 * Returns: capacity limit, in bytes
 * Since: 0.1.0
 */
guint64
mwt_period_get_capacity_limit (MwtPeriod *self)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), 0);

  return self->capacity_limit;
}

/* Add @n repeat periods to #MwtPeriod:start and #MwtPeriod:end, and return a
 * new #GDateTime for each of them. @n must be positive.
 *
 * @out_start and @out_end are both (inout), ignoring and freeing whatever old
 * value is passed in, and returning the result.
 *
 * If either of the dates could not be updated (if we’ve hit the limits in the
 * date type, for example), %FALSE will be returned and @out_start and @out_end
 * will be set to %NULL.
 *
 * The #MwtPeriod:repeat-type must not be %MWT_PERIOD_REPEAT_NONE, and the
 * #MwtPeriod:repeat-period must not be zero.
 *
 * @out_was_empty is set to %TRUE (and %FALSE is returned) if the nth recurrence
 * of the period was empty (for example, due to DST adjustments). See
 * get_nth_recurrence_skip_empty() for the recommended way to handle this.
 */
static gboolean
get_nth_recurrence (MwtPeriod  *self,
                    guint64     n,
                    GDateTime **out_start,
                    GDateTime **out_end,
                    gboolean   *out_was_empty)
{
  g_autoptr(GDateTime) new_start = NULL;
  g_autoptr(GDateTime) new_end = NULL;

  g_assert (self->repeat_period != 0);
  g_assert (n != 0);

  if (n > G_MAXINT / self->repeat_period)
    goto done;

  gint addand = self->repeat_period * n;

  switch (self->repeat_type)
    {
    case MWT_PERIOD_REPEAT_HOUR:
      new_start = g_date_time_add_hours (self->start, addand);
      new_end = g_date_time_add_hours (self->end, addand);
      break;
    case MWT_PERIOD_REPEAT_DAY:
      new_start = g_date_time_add_days (self->start, addand);
      new_end = g_date_time_add_days (self->end, addand);
      break;
    case MWT_PERIOD_REPEAT_WEEK:
      new_start = g_date_time_add_weeks (self->start, addand);
      new_end = g_date_time_add_weeks (self->end, addand);
      break;
    case MWT_PERIOD_REPEAT_MONTH:
      new_start = g_date_time_add_months (self->start, addand);
      new_end = g_date_time_add_months (self->end, addand);
      break;
    case MWT_PERIOD_REPEAT_YEAR:
      new_start = g_date_time_add_years (self->start, addand);
      new_end = g_date_time_add_years (self->end, addand);
      break;
    case MWT_PERIOD_REPEAT_NONE:
      /* Must be handled by the caller. */
    default:
      g_assert_not_reached ();
    }

done:
  /* We must not return one date without the other. If one, but not both, of the
   * g_date_time_add_*() calls failed, squash the other result. */
  if (new_start == NULL || new_end == NULL)
    {
      g_clear_pointer (&new_start, g_date_time_unref);
      g_clear_pointer (&new_end, g_date_time_unref);
    }

  gboolean was_empty = FALSE;

  if (new_start != NULL && new_end != NULL &&
      g_date_time_equal (new_start, new_end))
    {
      g_clear_pointer (&new_start, g_date_time_unref);
      g_clear_pointer (&new_end, g_date_time_unref);
      was_empty = TRUE;
    }

  g_assert (new_start == NULL || new_end == NULL ||
            g_date_time_compare (new_start, new_end) < 0);
  g_assert (!was_empty || (new_start == NULL && new_end == NULL));

  g_clear_pointer (out_start, g_date_time_unref);
  g_clear_pointer (out_end, g_date_time_unref);
  *out_start = g_steal_pointer (&new_start);
  *out_end = g_steal_pointer (&new_end);
  *out_was_empty = was_empty;

  return (*out_start != NULL && *out_end != NULL);
}

/* Version of get_nth_recurrence() which skips over empty recurrences.
 * @inout_n_skipped_periods must be provided, as the number of recurrences which
 * have already been skipped; it will be updated to include any additional skips
 * from this function call. Otherwise, semantics are the same as
 * get_nth_recurrence(). */
static gboolean
get_nth_recurrence_skip_empty (MwtPeriod  *self,
                               guint64     n,
                               GDateTime **out_start,
                               GDateTime **out_end,
                               guint64    *inout_n_skipped_periods)
{
  gboolean retval;
  gboolean was_empty;
  guint64 n_skipped_periods = *inout_n_skipped_periods;

  do
    {
      /* @n_skipped_periods should never be able to overflow, since a guint64
       * can store every second in the range of #GDateTime. */
      g_assert (n_skipped_periods <= G_MAXUINT64 - n);

      retval = get_nth_recurrence (self, n + n_skipped_periods,
                                   out_start, out_end, &was_empty);
      if (was_empty)
        n_skipped_periods++;
    }
  while (was_empty);

  g_debug ("%s: returning %s, n_skipped_periods: %" G_GUINT64_FORMAT " → %" G_GUINT64_FORMAT,
           G_STRFUNC, retval ? "yes" : "no", *inout_n_skipped_periods, n_skipped_periods);

  *inout_n_skipped_periods = n_skipped_periods;
  return retval;
}

/**
 * get_nearest_recurrences:
 * @self: a #MwtPeriod
 * @when: the time to check
 * @out_contains_start: (optional) (nullable) (out) (transfer full): return
 *    location for the start time of the recurrence containing @when
 * @out_contains_end: (optional) (nullable) (out) (transfer full): return
 *    location for the end time of the recurrence containing @when
 * @out_next_start: (optional) (nullable) (out) (transfer full): return
 *    location for the start time of the recurrence after @when
 * @out_next_end: (optional) (nullable) (out) (transfer full): return
 *    location for the end time of the recurrence after @when
 *
 * Get the recurrence of the #MwtPeriod which contains @when, and the next
 * recurrence after that. Either or both of these may be %NULL:
 *
 *  - The recurrence containing @when may be %NULL if no recurrence of the
 *    period actually contains @when.
 *  - The next recurrence may be %NULL if the #MwtPeriod:repeat-type is
 *    %MWT_PERIOD_REPEAT_NONE.
 *  - Or if the next recurrence would exceed the limits of #GDateTime (the year
 *    9999).
 *
 * If a recurrence is returned, both the start and end #GDateTimes are
 * guaranteed to be non-%NULL. The start and end times are guaranteed to be
 * in order, and non-equal. If a recurrence of the period is empty due to
 * falling in DST transition, it is skipped and the next non-empty recurrence is
 * returned.
 */
static void
get_nearest_recurrences (MwtPeriod  *self,
                         GDateTime  *when,
                         GDateTime **out_contains_start,
                         GDateTime **out_contains_end,
                         GDateTime **out_next_start,
                         GDateTime **out_next_end)
{
  g_autoptr(GDateTime) retval_contains_start = NULL;
  g_autoptr(GDateTime) retval_contains_end = NULL;
  g_autoptr(GDateTime) retval_next_start = NULL;
  g_autoptr(GDateTime) retval_next_end = NULL;

  /* We have to allocate these here to avoid memory corruption due to the
   * combination of g_autoptr() and goto. This is unfortunate, since the goto
   * in this function is fairly useful.
   * See: https://blog.fishsoup.net/2015/11/05/attributecleanup-mixed-declarations-and-code-and-goto/ */
  g_autoptr(GDateTime) start = NULL;
  g_autoptr(GDateTime) end = NULL;

  guint64 n_skipped_periods = 0;

  /* Get the base time if @when is %NULL, or if @when is before the base start
   * time. */
  if (when == NULL || g_date_time_compare (when, self->start) < 0)
    {
      retval_next_start = g_date_time_ref (self->start);
      retval_next_end = g_date_time_ref (self->end);
      goto done;
    }

  /* We can assume this from this point onwards. */
  g_assert (when != NULL);

  /* Does the base time for the period contain @when? */
  if (g_date_time_compare (self->start, when) <= 0 &&
      g_date_time_compare (when, self->end) < 0)
    {
      retval_contains_start = g_date_time_ref (self->start);
      retval_contains_end = g_date_time_ref (self->end);

      if (self->repeat_type != MWT_PERIOD_REPEAT_NONE)
        {
          /* Failure here will result in (@retval_next_start == retval_next_end == NULL),
           * which is what we want. */
          get_nth_recurrence_skip_empty (self, 1,
                                         &retval_next_start, &retval_next_end, &n_skipped_periods);
        }

      goto done;
    }

  /* Do recurrences happen at all? */
  if (self->repeat_type == MWT_PERIOD_REPEAT_NONE ||
      self->repeat_period == 0)
    goto done;

  /* Firstly, work out a lower bound on the number of periods which could have
   * elapsed between @self->start and @when. We can use this to jump ahead to
   * roughly when the most appropriate recurrence could happen to contain @when,
   * avoiding a load of iterations and #GDateTime allocations. */
  GTimeSpan max_period_span;

  switch (self->repeat_type)
    {
    case MWT_PERIOD_REPEAT_HOUR:
      max_period_span = G_TIME_SPAN_HOUR;
      break;
    case MWT_PERIOD_REPEAT_DAY:
      max_period_span = G_TIME_SPAN_DAY;
      break;
    case MWT_PERIOD_REPEAT_WEEK:
      max_period_span = G_TIME_SPAN_DAY * 7;
      break;
    case MWT_PERIOD_REPEAT_MONTH:
      /* The longest month has 31 days. Go for 32 days just in case there’s a
       * DST transition during the month (lengthening it by 1 hour). */
      max_period_span = G_TIME_SPAN_DAY * 32;
      break;
    case MWT_PERIOD_REPEAT_YEAR:
      /* A year is typically 365 days. Go for 367 just in case of added time
       * in DST transitions or it being a leap year. */
      max_period_span = G_TIME_SPAN_DAY * 367;
      break;
    case MWT_PERIOD_REPEAT_NONE:
      /* Handled above. */
    default:
      g_assert_not_reached ();
    }

  GTimeSpan diff = g_date_time_difference (when, self->start);
  g_assert (diff >= 0);
  g_assert (max_period_span != 0 && self->repeat_period != 0);
  guint64 min_n_periods = ((diff / max_period_span) / self->repeat_period);

  g_debug ("%s: diff: %" G_GINT64_FORMAT ", min_n_periods: %" G_GUINT64_FORMAT,
           G_STRFUNC, diff, min_n_periods);

  start = g_date_time_ref (self->start);
  end = g_date_time_ref (self->end);

  if (min_n_periods > 0 &&
      !get_nth_recurrence_skip_empty (self, min_n_periods,
                                      &start, &end, &n_skipped_periods))
    goto done;

  /* Add periods individually until we either match or overshoot. We need to be
   * careful to always add from the initial date, as addition to dates is not
   * transitive. For example, if a period spans 01:00 to 02:00 one week, and a
   * DST transition happens the next week where the clocks go forward by 1h at
   * 01:00, adding 1 week to the initial @start/@end date/times will produce
   * times of 02:00 to 02:00, not 01:00 to 02:00; adding 1 week more to those
   * date/times will not undo the error. However, adding 2 weeks from the
   * original date/times will avoid the times getting adjusted to avoid the
   * missing DST time at all; the errors will not compound. */
  for (guint64 i = 1; g_date_time_compare (start, when) <= 0; i++)
    {
      /* @min_n_periods + @i can never really overflow, given that a guint64 can
       * represent every second in the range of a #GDateTime, and the minimum
       * interval we operate in is hours. */
      g_assert (i <= G_MAXUINT64 - min_n_periods);

      /* Match now? */
      if (g_date_time_compare (start, when) <= 0 &&
          g_date_time_compare (when, end) < 0)
        {
          retval_contains_start = g_date_time_ref (start);
          retval_contains_end = g_date_time_ref (end);

          /* Failure here will result in (@retval_next_start == retval_next_end == NULL),
           * which is what we want. */
          get_nth_recurrence_skip_empty (self, min_n_periods + i,
                                         &retval_next_start, &retval_next_end, &n_skipped_periods);

          goto done;
        }

      if (!get_nth_recurrence_skip_empty (self, min_n_periods + i,
                                          &start, &end, &n_skipped_periods))
        goto done;
    }

  /* If we’ve reached this point, we have (@start > @when), so there is no
   * recurrence which contains @when, but we should return the start and end
   * times of the next recurrence. */
  g_assert (g_date_time_compare (start, when) > 0);
  g_assert (retval_contains_start == NULL);
  g_assert (retval_contains_end == NULL);
  retval_next_start = g_date_time_ref (start);
  retval_next_end = g_date_time_ref (end);
  goto done;

done:
  g_assert ((retval_contains_start == NULL) == (retval_contains_end == NULL));
  g_assert ((retval_next_start == NULL) == (retval_next_end == NULL));
  g_assert (retval_contains_start == NULL || when == NULL ||
            g_date_time_compare (retval_contains_start, when) <= 0);
  g_assert (retval_contains_end == NULL || when == NULL ||
            g_date_time_compare (retval_contains_end, when) > 0);
  g_assert (retval_next_start == NULL || when == NULL ||
            g_date_time_compare (retval_next_start, when) > 0);
  g_assert (retval_next_end == NULL || when == NULL ||
            g_date_time_compare (retval_next_end, when) > 0);
  g_assert (retval_contains_start == NULL || retval_contains_end == NULL ||
            g_date_time_compare (retval_contains_start, retval_contains_end) < 0);
  g_assert (retval_next_start == NULL || retval_next_end == NULL ||
            g_date_time_compare (retval_next_start, retval_next_end) < 0);
  g_assert (when != NULL || retval_contains_start == NULL);
  g_assert (when != NULL || retval_contains_end == NULL);
  g_assert (when != NULL || retval_next_start == self->start);
  g_assert (when != NULL || retval_next_end == self->end);

  if (out_contains_start != NULL)
    *out_contains_start = g_steal_pointer (&retval_contains_start);
  if (out_contains_end != NULL)
    *out_contains_end = g_steal_pointer (&retval_contains_end);
  if (out_next_start != NULL)
    *out_next_start = g_steal_pointer (&retval_next_start);
  if (out_next_end != NULL)
    *out_next_end = g_steal_pointer (&retval_next_end);
}

/**
 * mwt_period_contains_time:
 * @self: a #MwtPeriod
 * @when: the time to check
 * @out_start: (optional) (out) (transfer full) (nullable): return location for
 *    the start time of the recurrence which contains @when
 * @out_end: (optional) (out) (transfer full) (nullable): return location for
 *    the end time of the recurrence which contains @when
 *
 * Check whether @when lies within the given #MwtPeriod or any of its
 * recurrences. If it does, @out_start and @out_end will (if provided) will be
 * set to the start and end times of the recurrence which contains @when.
 *
 * If @when does not fall within a recurrence of the #MwtPeriod, @out_start
 * and @out_end will be set to %NULL.
 *
 * Returns: %TRUE if @when lies in the period, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_period_contains_time (MwtPeriod  *self,
                          GDateTime  *when,
                          GDateTime **out_start,
                          GDateTime **out_end)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), FALSE);
  g_return_val_if_fail (when != NULL, FALSE);

  g_autoptr(GDateTime) retval_start = NULL;
  g_autoptr(GDateTime) retval_end = NULL;

  /* Get the recurrence which contains @when, then. If no recurrence contains
   * @when, @retval_start and @retval_end will be set to %NULL. */
  get_nearest_recurrences (self, when, &retval_start, &retval_end, NULL, NULL);

  gboolean retval = (retval_start != NULL && retval_end != NULL);

  if (out_start != NULL)
    *out_start = g_steal_pointer (&retval_start);
  if (out_end != NULL)
    *out_end = g_steal_pointer (&retval_end);

  return retval;
}

/**
 * mwt_period_get_next_recurrence:
 * @self: a #MwtPeriod
 * @after: (nullable): time to get the next recurrence after
 * @out_next_start: (out) (optional) (nullable) (transfer full): return location
 *    for the start time of the next recurrence after @after
 * @out_next_end: (out) (optional) (nullable) (transfer full): return location
 *    for the end time of the next recurrence after @after
 *
 * Get the start and end time of the first recurrence of #MwtPeriod with a start
 * time greater than @after. If @after is %NULL, this will be the base start and
 * end time of the #MwtPeriod.
 *
 * If #MwtPeriod:repeat-type is %MWT_PERIOD_REPEAT_NONE, and @after is
 * non-%NULL, @out_next_start and @out_next_end will be set to %NULL and %FALSE
 * will be returned.
 *
 * If the first recurrence after @after exceeds the limits of #GDateTime (the
 * end of the year 9999), @out_next_start and @out_next_end will be set to %NULL
 * and %FALSE will be returned.
 *
 * If a recurrence is returned, both @out_next_start and @out_next_end are
 * guaranteed to be non-%NULL (if provided). The returned recurrence is
 * guaranteed to be non-empty: if the next recurrence after @after would be
 * empty due to a DST transition, the first following non-empty recurrence will
 * be returned.
 *
 * Returns: %TRUE if a next recurrence was found, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_period_get_next_recurrence (MwtPeriod  *self,
                                GDateTime  *after,
                                GDateTime **out_next_start,
                                GDateTime **out_next_end)
{
  g_return_val_if_fail (MWT_IS_PERIOD (self), FALSE);

  g_autoptr(GDateTime) retval_next_start = NULL;
  g_autoptr(GDateTime) retval_next_end = NULL;

  /* Get the recurrence which contains @when, then. If no recurrence contains
   * @when, @retval_start and @retval_end will be set to %NULL. */
  get_nearest_recurrences (self, after, NULL, NULL, &retval_next_start, &retval_next_end);

  gboolean retval = (retval_next_start != NULL && retval_next_end != NULL);

  if (out_next_start != NULL)
    *out_next_start = g_steal_pointer (&retval_next_start);
  if (out_next_end != NULL)
    *out_next_end = g_steal_pointer (&retval_next_end);

  return retval;
}
