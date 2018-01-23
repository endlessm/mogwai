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
   * The default is %_MAXUINT64 (no limit).
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
