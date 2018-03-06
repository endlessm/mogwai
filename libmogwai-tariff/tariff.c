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
   * The periods are guaranteed to be ordered by decreasing time span, and then
   * by increasing start date/time.
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
 *   ¬ (p_1.start < p_2.start ∧
 *      p_1.end > p_2.start ∧
 *      p_1.end < p_2.end) ∧
 *   ¬ (p_1.start = p_2.start ∧
 *      p_1.end = p_2.end)
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
           * or (when i and j have swapped)
           * p_1:  ▀▀▀
           * p_2: ▀▀▀
           */
          if (g_date_time_compare (p_1_start, p_2_start) < 0 &&
              g_date_time_compare (p_1_end, p_2_start) > 0 &&
              g_date_time_compare (p_1_end, p_2_end) < 0)
            return FALSE;

          /* p_1: ▀▀▀
           * p_2: ▀▀▀
           */
          if (g_date_time_compare (p_1_start, p_2_start) == 0 &&
              g_date_time_compare (p_1_end, p_2_end) == 0)
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

      if (mwt_period_contains_time (period, when, NULL, NULL))
        g_ptr_array_add (matches, period);
    }

  /* Pick the shortest period. There should be no ties here, since overlapping
   * periods are disallowed. */
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

static void
update_earliest (GDateTime **earliest,
                 MwtPeriod **earliest_from_period,
                 MwtPeriod **earliest_to_period,
                 GDateTime  *maybe_earlier,
                 MwtPeriod  *maybe_earlier_from_period,
                 MwtPeriod  *maybe_earlier_to_period)
{
  /* Update @earliest if @maybe_earlier is the same time, as we sort
   * self->periods by decreasing timespan, and the shorter periods always take
   * priority over the longer ones. */
  if (*earliest == NULL || g_date_time_compare (maybe_earlier, *earliest) <= 0)
    {
      g_clear_pointer (earliest, g_date_time_unref);
      *earliest = g_date_time_ref (maybe_earlier);
      *earliest_from_period = maybe_earlier_from_period;
      *earliest_to_period = maybe_earlier_to_period;
    }
}

typedef struct
{
  GDateTime *when;  /* (owned) */
  enum
    {
      TRANSITION_FROM,
      TRANSITION_TO,
    } type;
  gsize period_index;
  MwtPeriod *period;  /* (unowned) */
} TransitionData;

static void
transition_data_clear (TransitionData *data)
{
  g_clear_pointer (&data->when, g_date_time_unref);
}

static gint
transition_data_sort (const TransitionData *a,
                      const TransitionData *b)
{
  gint when_comparison = g_date_time_compare (a->when, b->when);

  if (when_comparison != 0)
    return when_comparison;

  /* Order %TRANSITION_FROM before %TRANSITION_TO. */
  if (a->type == TRANSITION_FROM && b->type == TRANSITION_TO)
    return -1;
  else if (a->type == TRANSITION_TO && b->type == TRANSITION_FROM)
    return 1;

  /* If the transition types are equal, order by length/start time. That’s
   * equivalent to ordering by the reverse index of @period in self->periods,
   * due to the sort order we guarantee on the latter. */
  if (a->period_index > b->period_index)
    return -1;
  else if (a->period_index < b->period_index)
    return 1;

  g_assert_not_reached ();
}

/* Get the next transition from @transitions after @data_index whose date/time
 * is greater than that of @data. If no such transition exists, return %NULL. */
static const TransitionData *
get_next_transition (GArray               *transitions,
                     gsize                 data_index,
                     const TransitionData *data)
{
  if (data_index == G_MAXSIZE)
    return NULL;

  for (gsize next_data_index = data_index + 1; next_data_index < transitions->len; next_data_index++)
    {
      const TransitionData *next_data =
          &g_array_index (transitions, TransitionData, next_data_index);

      if (!g_date_time_equal (next_data->when, data->when))
        return next_data;
    }

  return NULL;
}

/**
 * mwt_tariff_get_next_transition:
 * @self: a #MwtTariff
 * @after: (nullable): time to get the next transition after
 * @out_from_period: (out) (optional) (nullable) (transfer none): return
 *    location for the period being transitioned out of
 * @out_to_period: (out) (optional) (nullable) (transfer none): return location
 *    for the period being transitioned in to
 *
 * Get the date and time of the first transition between periods after @after in
 * this #MwtTariff, and return the periods being transitioned out of and in to
 * in @out_from_period and @out_to_period.
 *
 * If @after is %NULL, the first transition in the tariff is returned:
 * @out_from_period is guaranteed to be %NULL, @out_to_period is guaranteed to
 * be non-%NULL, and a non-%NULL value is guaranteed to be returned.
 *
 * Either or both of @out_from_period and @out_to_period may be %NULL, if the
 * next transition is into the first tariff of the period, out of the last
 * tariff of the period; or if there are no more transitions after @after. It is
 * possible for @out_from_period and @out_to_period to be set to the same
 * #MwtPeriod instance, if one recurrence of the period ends when the next
 * begins.
 *
 * If a non-%NULL date and time is returned, at least one of @out_from_period
 * and @out_to_period (if provided) are guaranteed to be non-%NULL.
 *
 * Returns: (nullable) (transfer full): date and time of the next transition,
 *    or %NULL if there are no more transitions after @after
 * Since: 0.1.0
 */
GDateTime *
mwt_tariff_get_next_transition (MwtTariff  *self,
                                GDateTime  *after,
                                MwtPeriod **out_from_period,
                                MwtPeriod **out_to_period)
{
  g_return_val_if_fail (MWT_IS_TARIFF (self), NULL);

  /* If (@after == NULL), we need to get the first transition. */
  if (after == NULL)
    {
      g_autoptr(GDateTime) first_transition = NULL;
      MwtPeriod *first_from_period = NULL;
      MwtPeriod *first_to_period = NULL;

      /* Periods are stored ordered by time span first, so we can’t just use the
       * first one. */
      for (gsize i = 0; i < self->periods->len; i++)
        {
          MwtPeriod *period = g_ptr_array_index (self->periods, i);

          update_earliest (&first_transition, &first_from_period, &first_to_period,
                           mwt_period_get_start (period), NULL, period);
        }

      g_assert (first_transition != NULL);
      g_assert (first_from_period == NULL);
      g_assert (first_to_period != NULL);
      g_assert (mwt_period_contains_time (first_to_period, first_transition, NULL, NULL));

      if (out_from_period)
        *out_from_period = first_from_period;
      if (out_to_period)
        *out_to_period = first_to_period;

      return g_steal_pointer (&first_transition);
    }

  /* Otherwise, get the next transition for each of the periods in the tariff.
   * For each period, if the period contains @after, then we know its next
   * transition will be FROM that period to another. Otherwise, if the period
   * has another recurrence after @after, we know its next transition will be
   * TO that period from another. Build this up into an array of
   * #TransitionData. */
  g_autoptr(GDateTime) next_transition = NULL;
  MwtPeriod *next_from_period = NULL;
  MwtPeriod *next_to_period = NULL;

  g_autoptr(GArray) transitions = g_array_sized_new (FALSE, FALSE,
                                                     sizeof (TransitionData),
                                                     self->periods->len);
  g_array_set_clear_func (transitions, (GDestroyNotify) transition_data_clear);

  for (gsize i = 0; i < self->periods->len; i++)
    {
      MwtPeriod *period = g_ptr_array_index (self->periods, i);

      g_array_set_size (transitions, transitions->len + 1);
      TransitionData *data = &g_array_index (transitions, TransitionData,
                                             transitions->len - 1);

      if (mwt_period_contains_time (period, after, NULL, &data->when))
        {
          g_assert (g_date_time_compare (data->when, after) > 0);
          data->type = TRANSITION_FROM;
          data->period_index = i;
          data->period = period;
        }
      else if (mwt_period_get_next_recurrence (period, after, &data->when, NULL))
        {
          g_assert (g_date_time_compare (data->when, after) > 0);
          data->type = TRANSITION_TO;
          data->period_index = i;
          data->period = period;
        }
      else
        {
          /* This period has no more transitions; erase the prospective entry
           * from @transitions. */
          g_array_set_size (transitions, transitions->len - 1);
        }
    }

  /* Is @transitions empty? */
  if (transitions->len == 0)
    goto done;

  /* Sort the transitions. */
  g_array_sort (transitions, (GCompareFunc) transition_data_sort);

  /* All the transitions in @transitions are guaranteed to be equal to or later
   * than @after. So far, the @next_transition_data only contains *one* of the
   * periods which the transition enters/leaves. Now we need to work out what
   * the other period is. It may be %NULL. */
  const TransitionData *next_transition_data =
      &g_array_index (transitions, TransitionData, 0);
  g_assert (g_date_time_compare (after, next_transition_data->when) < 0);

  switch (next_transition_data->type)
    {
    case TRANSITION_FROM:
        {
          /* In this case, @next_transition_data concerns a transition FROM one
           * period to another. The #TransitionData.when field is the end
           * date/time of a recurrence of the period given in
           * #TransitionData.period. Find the following transition. If it’s also
           * a FROM transition, we have one period nested within another, and
           * the transition we care about (@next_transition_data) is FROM the
           * inner to the outer. If it’s a TO transition instead, there could be
           * several different valid arrangements of periods, and the easiest
           * way to find the period the transition is going to is by calling
           * mwt_tariff_lookup_period() for the end of the current one. */
          const TransitionData *following_transition_data =
              get_next_transition (transitions, 0, next_transition_data);
          next_transition = g_date_time_ref (next_transition_data->when);
          next_from_period = next_transition_data->period;
          if (following_transition_data != NULL &&
              following_transition_data->type == TRANSITION_FROM)
            next_to_period = following_transition_data->period;
          else
            next_to_period = mwt_tariff_lookup_period (self, next_transition);
        }
      break;
    case TRANSITION_TO:
        {
          next_transition = g_date_time_ref (next_transition_data->when);
          next_to_period = next_transition_data->period;

          /* In this case, we could have two transitions, FROM and TO, with
           * @after sitting between them. There could be a period which spans
           * from before FROM to after TO. To detect this, we’d have to walk
           * backwards through the list of transitions (potentially needing
           * to generate new ones), bracketing them, until we found an
           * unmatched TO transition. It seems easier to do this hack. I am
           * not happy with this hack, but it does mean saving a lot of code
           * and testing. The only algorithmically nice fix for this is to
           * rewrite everything to use an interval tree.
           * FIXME: Should we put lower bounds on the permitted lengths of periods? */
          g_autoptr(GDateTime) before_next_transition = g_date_time_add_seconds (next_transition, -1.0);
          next_from_period = mwt_tariff_lookup_period (self, before_next_transition);
        }
      break;
    default:
      g_assert_not_reached ();
    }

done:
  g_assert (next_transition != NULL || next_from_period == NULL);
  g_assert (next_transition != NULL || next_to_period == NULL);
  g_assert (next_transition == NULL ||
            (next_from_period != NULL || next_to_period != NULL));
  g_assert (next_transition == NULL ||
            g_date_time_compare (next_transition, after) > 0);
  g_assert (next_transition == NULL || next_to_period == NULL ||
            mwt_period_contains_time (next_to_period, next_transition, NULL, NULL));

  if (out_from_period != NULL)
    *out_from_period = next_from_period;
  if (out_to_period != NULL)
    *out_to_period = next_to_period;

  return g_steal_pointer (&next_transition);
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
