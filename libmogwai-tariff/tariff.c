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
#include <libmogwai-tariff/period.h>
#include <libmogwai-tariff/tariff.h>
#include <string.h>


G_DEFINE_QUARK (MwtTariffError, mwt_tariff_error)

static void mwt_tariff_constructed  (GObject      *object);
static void mwt_tariff_dispose      (GObject      *object);
static void mwt_tariff_get_property (GObject      *object,
                                     guint         property_id,
                                     GValue       *value,
                                     GParamSpec   *pspec);
static void mwt_tariff_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);

/**
 * MwtTariff:
 *
 * A representation of a network tariff. Tariffs are represented as a non-empty
 * set of time periods (#MwtPeriod), each of which has a constant set of
 * properties, such as bandwidth or capacity limits which apply over that
 * period.
 *
 * The periods in a tariff must be non-overlapping, in the sense that if a
 * period intersects another period at all, it must be entirely contained
 * within that period. The properties which apply to a given date/time are
 * selected from the shortest period which contains that date/time — see
 * mwt_tariff_lookup_period().
 *
 * The valid relative positions of any two periods are as follows, where the
 * first line gives the first period (A), and all subsequent lines are the valid
 * positions for a second period (B):
 *
 * |[
 *   A:     ▀▀▀
 *   B: ▀▀▀
 *       ▀▀▀
 *             ▀▀▀
 *              ▀▀▀
 *          ▀
 *           ▀
 *            ▀
 *         ▀▀▀▀▀
 *         ▀▀▀▀
 *          ▀▀▀▀
 * ]|
 *
 * Similarly, here are the invalid relative positions:
 *
 * |[
 *   A:   ▀▀▀
 *   B: ▀▀▀
 *        ▀▀▀
 *          ▀▀▀
 * ]|
 *
 * The periods in a tariff must also be ordered by decreasing span, and then by
 * increasing start date/time. Two periods are not allowed to be equal in span
 * and start date/time. There must be at least one period in a tariff.
 *
 * The #MwtTariff class is immutable once loaded or constructed.
 *
 * Since: 0.1.0
 */
struct _MwtTariff
{
  GObject parent;

  gchar *name;  /* (owned) */
  GPtrArray *periods;  /*  (element-type MwtPeriod) (owned) */
};

typedef enum
{
  PROP_NAME = 1,
  PROP_PERIODS,
} MwtTariffProperty;

G_DEFINE_TYPE (MwtTariff, mwt_tariff, G_TYPE_OBJECT)

static void
mwt_tariff_class_init (MwtTariffClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_PERIODS + 1] = { NULL, };

  object_class->constructed = mwt_tariff_constructed;
  object_class->dispose = mwt_tariff_dispose;
  object_class->get_property = mwt_tariff_get_property;
  object_class->set_property = mwt_tariff_set_property;

  /**
   * MwtTariff:name:
   *
   * Unique name of the tariff. This is for identifying the tariff, and is not
   * necessarily human readable. It must be non-empty, a valid filename (so must
   * not contain `/` or `\\`), and valid UTF-8. It must also only contain
   * characters which are valid for internationalised domain names, as per
   * RFC 3491.
   *
   * Since: 0.1.0
   */
  props[PROP_NAME] =
      g_param_spec_string ("name", "Name",
                           "Unique name of the tariff.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * MwtTariff:periods: (element-type MwtPeriod)
   *
   * The set of #MwtPeriods in the tariff. See the documentation for #MwtTariff
   * for the requirements about periods being non-overlapping, etc.
   *
   * The periods are guaranteed to be ordered by time span, and then by start
   * date/time.
   *
   * Since: 0.1.0
   */
  props[PROP_PERIODS] =
      g_param_spec_boxed ("periods", "Periods",
                          "The set of #MwtPeriods in the tariff.",
                          G_TYPE_PTR_ARRAY,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
mwt_tariff_init (MwtTariff *self)
{
  /* Nothing to do here. */
}

static void
mwt_tariff_constructed (GObject *object)
{
  MwtTariff *self = MWT_TARIFF (object);

  G_OBJECT_CLASS (mwt_tariff_parent_class)->constructed (object);

  /* Validate the properties. */
  g_assert (mwt_tariff_validate (self->name, self->periods, NULL));
}

static void
mwt_tariff_dispose (GObject *object)
{
  MwtTariff *self = MWT_TARIFF (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->periods, g_ptr_array_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwt_tariff_parent_class)->dispose (object);
}

static void
mwt_tariff_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MwtTariff *self = MWT_TARIFF (object);

  switch ((MwtTariffProperty) property_id)
    {
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;
    case PROP_PERIODS:
      g_value_set_boxed (value, self->periods);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mwt_tariff_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  MwtTariff *self = MWT_TARIFF (object);

  switch ((MwtTariffProperty) property_id)
    {
    case PROP_NAME:
      /* Construct only. */
      g_assert (self->name == NULL);
      self->name = g_value_dup_string (value);
      break;
    case PROP_PERIODS:
      /* Construct only. */
      g_assert (self->periods == NULL);
      self->periods = g_value_dup_boxed (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

/* ∀ p_1, p_2 ∈ self->periods.
 *   ¬ (p_1.start ≤ p_2.start ∧
 *      p_1.end > p_2.start ∧
 *      p_1.end ≤ p_2.end)
 */
static gboolean
mwt_tariff_are_periods_nonoverlapping (GPtrArray *periods)
{
  /* FIXME: This is O(N^2) at the moment. We assume there are not many periods. */
  /* FIXME: This needs to expand recurrences, since we could have two periods
   * whose spans are identical, but whose start times differ by the repeat period
   * of one of them. */
  for (gsize i = 0; i < periods->len; i++)
    {
      MwtPeriod *p_1 = g_ptr_array_index (periods, i);
      GDateTime *p_1_start = mwt_period_get_start (p_1);
      GDateTime *p_1_end = mwt_period_get_end (p_1);

      for (gsize j = 0; j < periods->len; j++)
        {
          MwtPeriod *p_2 = g_ptr_array_index (periods, j);
          GDateTime *p_2_start = mwt_period_get_start (p_2);
          GDateTime *p_2_end = mwt_period_get_end (p_2);

          if (i == j)
            continue;

          /* p_1: ▀▀▀
           * p_2:  ▀▀▀
           */
          if (g_date_time_compare (p_1_start, p_2_start) < 0 &&
              g_date_time_compare (p_1_end, p_2_start) >= 0 &&
              g_date_time_compare (p_1_end, p_2_end) < 0)
            return FALSE;

          /* p_1: ▀▀▀
           * p_2: ▀▀▀
           */
          if (g_date_time_compare (p_1_start, p_2_start) == 0 &&
              g_date_time_compare (p_1_end, p_2_end) == 0)
            return FALSE;

          /* p_1:  ▀▀▀
           * p_2: ▀▀▀
           */
          if (g_date_time_compare (p_1_start, p_2_start) > 0 &&
              g_date_time_compare (p_1_start, p_2_end) < 0 &&
              g_date_time_compare (p_1_end, p_2_end) > 0)
            return FALSE;
        }
    }

  return TRUE;
}

/* Periods must be ordered by decreasing time span, and then by increasing start
 * date/time. */
static gboolean
mwt_tariff_are_periods_ordered (GPtrArray *periods)
{
  for (gsize i = 1; i < periods->len; i++)
    {
      MwtPeriod *p1 = g_ptr_array_index (periods, i - 1);
      MwtPeriod *p2 = g_ptr_array_index (periods, i);
      GDateTime *p1_start = mwt_period_get_start (p1);
      GDateTime *p1_end = mwt_period_get_end (p1);
      GDateTime *p2_start = mwt_period_get_start (p2);
      GDateTime *p2_end = mwt_period_get_end (p2);

      GTimeSpan p1_span = g_date_time_difference (p1_end, p1_start);
      GTimeSpan p2_span = g_date_time_difference (p2_end, p2_start);

      if (p1_span < p2_span ||
          (p1_span == p2_span && g_date_time_compare (p1_start, p2_start) >= 0))
        return FALSE;
    }

  return TRUE;
}

/**
 * mwt_tariff_validate:
 * @naeme: (nullable): tariff name (see #MwtTariff:name)
 * @periods: (nullable): periods in the tariff (see #MwtTariff:periods)
 * @error: return location for a #GError, or %NULL
 *
 * Validate the given #MwtTariff properties, returning %MWT_TARIFF_ERROR_INVALID
 * if any of them are invalid. All inputs are allowed to the property arguments
 * (except @error): no inputs are a programmer error.
 *
 * It is guaranteed that if this function returns %TRUE for a given set of
 * inputs, mwt_tariff_new() will succeed for those inputs.
 *
 * Returns: %TRUE if valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_tariff_validate (const gchar  *name,
                     GPtrArray    *periods,
                     GError      **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!mwt_tariff_validate_name (name))
    {
      g_set_error_literal (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                           _("Invalid tariff name."));
      return FALSE;
    }

  if (periods == NULL ||
      periods->len == 0 ||
      !mwt_tariff_are_periods_nonoverlapping (periods) ||
      !mwt_tariff_are_periods_ordered (periods))
    {
      g_set_error_literal (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                           _("Invalid tariff periods."));
      return FALSE;
    }

  return TRUE;
}

/**
 * mwt_tariff_new:
 * @start: tariff name (see #MwtTariff:name)
 * @periods: periods in the tariff (see #MwtTariff:periods)
 *
 * Create a #MwtTariff object with the given properties.
 *
 * All inputs to this function must have been validated with
 * mwt_tariff_validate() first. It is a programmer error to provide invalid
 * inputs.
 *
 * Returns: (transfer full): a new #MwtTariff
 * Since: 0.1.0
 */
MwtTariff *
mwt_tariff_new (const gchar *name,
                GPtrArray   *periods)
{
  /* Leave property validation to constructed(). */
  return g_object_new (MWT_TYPE_TARIFF,
                       "name", name,
                       "periods", periods,
                       NULL);
}

/**
 * mwt_tariff_get_start:
 * @self: a #MwtTariff
 *
 * Get the value of #MwtTariff:name.
 *
 * Returns: tariff name
 * Since: 0.1.0
 */
const gchar *
mwt_tariff_get_name (MwtTariff *self)
{
  g_return_val_if_fail (MWT_IS_TARIFF (self), NULL);

  return self->name;
}

/**
 * mwt_tariff_get_periods:
 * @self: a #MwtTariff
 *
 * Get the value of #MwtTariff:periods.
 *
 * Returns: (element-type MwtPeriod): periods in the tariff
 * Since: 0.1.0
 */
GPtrArray *
mwt_tariff_get_periods (MwtTariff *self)
{
  g_return_val_if_fail (MWT_IS_TARIFF (self), NULL);

  return self->periods;
}

/**
 * mwt_tariff_lookup_period:
 * @self: a #MwtTariff
 * @when: the date/time to look up
 *
 * Look up the #MwtPeriod which applies to the given date/time. If @when lies
 * outside the overall start and end times of the tariff, %NULL will be
 * returned. It is up to the caller to treat this appropriately (for example, by
 * disallowing all downloads).
 *
 * This will expand the recurrences of each period in order to find matches.
 *
 * Returns: (transfer none) (nullable): period covering @when, or %NULL if no
 *    periods are relevant
 * Since: 0.1.0
 */
MwtPeriod *
mwt_tariff_lookup_period (MwtTariff *self,
                          GDateTime *when)
{
  g_return_val_if_fail (MWT_IS_TARIFF (self), NULL);
  g_return_val_if_fail (when != NULL, NULL);

  /* Find the set of periods which overlap @when, including taking recurrences
   * into account. */
  /* FIXME: We don’t expect there to be many periods in a tariff. If there are,
   * this algorithm could be improved to use an interval tree or similar to
   * improve performance. */
  g_autoptr(GPtrArray) matches = g_ptr_array_new_with_free_func (NULL);

  for (gsize i = 0; i < self->periods->len; i++)
    {
      MwtPeriod *period = g_ptr_array_index (self->periods, i);

      if (mwt_period_contains_time (period, when))
        g_ptr_array_add (matches, period);
    }

  /* Pick the shortest period. There should be no ties here, since having two
   * periods with an identical span and start date/time is disallowed. */
  MwtPeriod *shortest_period = NULL;
  GTimeSpan shortest_period_duration = G_MAXINT64;
  for (gsize i = 0; i < matches->len; i++)
    {
      MwtPeriod *period = g_ptr_array_index (matches, i);
      GDateTime *start = mwt_period_get_start (period);
      GDateTime *end = mwt_period_get_end (period);
      GTimeSpan duration = g_date_time_difference (end, start);

      g_assert (shortest_period == NULL || duration != shortest_period_duration);

      if (shortest_period == NULL || duration < shortest_period_duration)
        {
          shortest_period = period;
          shortest_period_duration = duration;
        }
    }

  return shortest_period;
}

/**
 * mwt_tariff_validate_name:
 * @name: (nullable): string to validate
 *
 * Validate the given @name string to see if it is a valid name for a tariff.
 * Any input is accepted (not a programming error), including %NULL.
 *
 * See #MwtTariff:name for the specification of valid names.
 *
 * Returns: %TRUE if @name is valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_tariff_validate_name (const gchar *name)
{
  if (name == NULL || *name == '\0')
    return FALSE;
  if (!g_utf8_validate (name, -1, NULL))
    return FALSE;
  if (strchr (name, '/') != NULL ||
      strchr (name, '\\') != NULL)
    return FALSE;

  /* Abuse g_hostname_to_ascii() for its IDN validation. */
  g_autofree gchar *ascii = g_hostname_to_ascii (name);
  if (ascii == NULL)
    return FALSE;

  return TRUE;
}
