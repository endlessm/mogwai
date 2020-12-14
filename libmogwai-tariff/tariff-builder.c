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
#include <libmogwai-tariff/tariff-builder.h>
#include <libmogwai-tariff/tariff.h>


static void mwt_tariff_builder_dispose (GObject *object);

/**
 * MwtTariffBuilder:
 *
 * A helper object for constructing an #MwtTariff and serialising it to a
 * #GBytes which can be transmitted or stored. See #MwtTariffLoader for the
 * inverse operation.
 *
 * When using a #MwtTariffBuilder, all the required properties of the tariff
 * must be set (including at least one period), then
 * mwt_tariff_builder_get_tariff() can be used to get the resulting #MwtTariff
 * object (similarly for the variants to return it as bytes or a #GVariant).
 * Before then, mwt_tariff_builder_get_tariff() will return %NULL.
 *
 * A #MwtTariffBuilder may be used multiple times, or an in-progress tariff may
 * be destroyed by using mwt_tariff_builder_reset().
 *
 * Since: 0.1.0
 */
struct _MwtTariffBuilder
{
  GObject parent;

  gchar *name;  /* (owned) */
  GPtrArray *periods;  /* (element-type MwtPeriod) (owned) */

  MwtTariff *final_tariff;  /* (nullable) (owned) */
  GVariant *final_variant;  /* (nullable) (owned) */
};

G_DEFINE_TYPE (MwtTariffBuilder, mwt_tariff_builder, G_TYPE_OBJECT)

static void
mwt_tariff_builder_class_init (MwtTariffBuilderClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = mwt_tariff_builder_dispose;
}

static void
mwt_tariff_builder_init (MwtTariffBuilder *self)
{
  self->periods = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
mwt_tariff_builder_dispose (GObject *object)
{
  MwtTariffBuilder *self = MWT_TARIFF_BUILDER (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->periods, g_ptr_array_unref);
  g_clear_object (&self->final_tariff);
  g_clear_pointer (&self->final_variant, g_variant_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwt_tariff_builder_parent_class)->dispose (object);
}

/**
 * mwt_tariff_builder_new:
 *
 * Create a new, empty #MwtTariffBuilder.
 *
 * Returns: (transfer full): a new #MwtTariffBuilder
 * Since: 0.1.0
 */
MwtTariffBuilder *
mwt_tariff_builder_new (void)
{
  return g_object_new (MWT_TYPE_TARIFF_BUILDER, NULL);
}

/**
 * mwt_tariff_builder_reset:
 * @self: a #MwtTariffBuilder
 *
 * Reset the state of the builder, clearing any completed or in-progress
 * tariffs.
 *
 * Since: 0.1.0
 */
void
mwt_tariff_builder_reset (MwtTariffBuilder *self)
{
  g_return_if_fail (MWT_IS_TARIFF_BUILDER (self));

  g_clear_pointer (&self->name, g_free);
  g_ptr_array_set_size (self->periods, 0);
  g_clear_object (&self->final_tariff);
  g_clear_pointer (&self->final_variant, g_variant_unref);
}

/**
 * mwt_tariff_builder_set_name:
 * @self: a #MwtTariffBuilder
 * @name: name for the new tariff
 *
 * Set the name for the tariff under construction. See #MwtTariff:name for
 * details of valid names.
 *
 * Since: 0.1.0
 */
void
mwt_tariff_builder_set_name (MwtTariffBuilder *self,
                             const gchar      *name)
{
  g_return_if_fail (MWT_IS_TARIFF_BUILDER (self));
  g_return_if_fail (mwt_tariff_validate_name (name));

  g_free (self->name);
  self->name = g_strdup (name);
}

/**
 * mwt_tariff_builder_add_period:
 * @self: a #MwtTariffBuilder
 * @period: (transfer none): a #MwtPeriod to add to the tariff
 *
 * Add the given #MwtPeriod to the tariff under construction. This may be called
 * multiple times for a given tariff, and must be called at least once per valid
 * tariff.
 *
 * Periods may be added in any order; they will be sorted before the tariff is
 * generated.
 *
 * Since: 0.1.0
 */
void
mwt_tariff_builder_add_period (MwtTariffBuilder *self,
                               MwtPeriod        *period)
{
  g_return_if_fail (MWT_IS_TARIFF_BUILDER (self));
  g_return_if_fail (MWT_IS_PERIOD (period));

  g_ptr_array_add (self->periods, g_object_ref (period));
}

/* Order by decreasing span, then by increasing start date/time. */
static gint
periods_sort_cb (gconstpointer a,
                 gconstpointer b)
{
  MwtPeriod *p1 = *((MwtPeriod **) a);
  MwtPeriod *p2 = *((MwtPeriod **) b);

  GDateTime *p1_start = mwt_period_get_start (p1);
  GDateTime *p1_end = mwt_period_get_end (p2);
  GDateTime *p2_start = mwt_period_get_start (p1);
  GDateTime *p2_end = mwt_period_get_end (p2);

  GTimeSpan p1_span = g_date_time_difference (p1_end, p1_start);
  GTimeSpan p2_span = g_date_time_difference (p2_end, p2_start);

  if (p1_span == p2_span)
    return g_date_time_compare (p1_start, p2_start);

  return (p1_span > p2_span) ? -1 : 1;
}

/**
 * mwt_tariff_builder_get_tariff:
 * @self: a #MwtTariffBuilder
 *
 * Get the newly constructed #MwtTariff, or %NULL if the builder is incomplete,
 * has been reset, or if there was an error building the tariff. The tariff can
 * be retrieved multiple times from this function; the builder is not reset
 * after this function is called.
 *
 * Returns: (transfer full) (nullable): the constructed #MwtTariff, or %NULL if
 *    one is not currently constructed
 * Since: 0.1.0
 */
MwtTariff *
mwt_tariff_builder_get_tariff (MwtTariffBuilder *self)
{
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (MWT_IS_TARIFF_BUILDER (self), NULL);

  /* If we haven’t constructed the final tariff yet, try. If it fails, just
   * return %NULL since either the builder is unused, or the developer has made
   * an error. */
  if (self->final_tariff == NULL)
    {
      /* Ensure the periods are in order. */
      g_ptr_array_sort (self->periods, periods_sort_cb);

      if (!mwt_tariff_validate (self->name, self->periods, &local_error))
        {
          g_debug ("Invalid tariff: %s", local_error->message);
          return NULL;
        }
      self->final_tariff = mwt_tariff_new (self->name, self->periods);
    }

  return g_object_ref (self->final_tariff);
}

/* Returns a new floating variant. */
static GVariant *
mwt_tariff_builder_build_tariff_variant (const gchar *name,
                                         MwtTariff   *tariff)
{
  const guint16 format_version = 2;
  const gchar *format_magic = "Mogwai tariff";
  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("(sa(ttssqut))"));

  g_variant_builder_add (&builder, "s", name);

  /* Periods. mwt_tariff_get_periods() guarantees it’s in order. */
  GPtrArray *periods = mwt_tariff_get_periods (tariff);
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(ttssqut)"));

  for (gsize i = 0; i < periods->len; i++)
    {
      MwtPeriod *period = g_ptr_array_index (periods, i);
      GDateTime *start = mwt_period_get_start (period);
      GTimeZone *start_tz = g_date_time_get_timezone (start);
      GDateTime *end = mwt_period_get_end (period);
      GTimeZone *end_tz = g_date_time_get_timezone (end);
      guint64 start_unix = g_date_time_to_unix (start);
      guint64 end_unix = g_date_time_to_unix (end);

      g_variant_builder_open (&builder, G_VARIANT_TYPE ("(ttssqut)"));
      g_variant_builder_add (&builder, "t", start_unix);
      g_variant_builder_add (&builder, "t", end_unix);
      g_variant_builder_add (&builder, "s", g_time_zone_get_identifier (start_tz));
      g_variant_builder_add (&builder, "s", g_time_zone_get_identifier (end_tz));
      g_variant_builder_add (&builder, "q", (guint16) mwt_period_get_repeat_type (period));
      g_variant_builder_add (&builder, "u", (guint32) mwt_period_get_repeat_period (period));
      g_variant_builder_add (&builder, "t", mwt_period_get_capacity_limit (period));
      g_variant_builder_close (&builder);
    }

  g_variant_builder_close (&builder);

  /* Add the file format version, which also acts as a byte order mark (so
   * explicitly don’t convert it to a known endianness). The magic bytes allow
   * content type detection. */
  return g_variant_new ("(sqv)",
                        format_magic, format_version,
                        g_variant_builder_end (&builder));
}

/**
 * mwt_tariff_builder_get_tariff_as_variant:
 * @self: a #MwtTariffBuilder
 *
 * Get the newly constructed tariff as a #GVariant. This will return %NULL in
 * exactly the same situations as when mwt_tariff_builder_get_tariff() returns
 * %NULL.
 *
 * The returned #GVariant is guaranteed to be in normal form, and a non-floating
 * reference will be returned.
 *
 * Returns: (transfer full) (nullable): a new, non-floating ref to the
 *    constructed tariff as a #GVariant, or %NULL if one is not currently
 *    constructed
 * Since: 0.1.0
 */
GVariant *
mwt_tariff_builder_get_tariff_as_variant (MwtTariffBuilder *self)
{
  g_return_val_if_fail (MWT_IS_TARIFF_BUILDER (self), NULL);

  if (self->final_variant == NULL)
    {
      g_autoptr(MwtTariff) tariff = mwt_tariff_builder_get_tariff (self);
      if (tariff == NULL)
        return NULL;

      g_autoptr(GVariant) variant = mwt_tariff_builder_build_tariff_variant (self->name, tariff);
      self->final_variant = g_variant_ref_sink (g_steal_pointer (&variant));
    }

  return g_variant_ref (self->final_variant);
}

/**
 * mwt_tariff_builder_get_tariff_as_bytes:
 * @self: a #MwtTariffBuilder
 *
 * Get the newly constructed tariff as a #GBytes. This will return %NULL in
 * exactly the same situations as when mwt_tariff_builder_get_tariff() returns
 * %NULL.
 *
 * The returned #GBytes is suitable to be written to a file or sent over the
 * network. Its byte ordering is encoded so it may be loaded on a system with
 * a different byte ordering.
 *
 * Returns: (transfer full) (nullable): the constructed tariff as a #GBytes,
 *    or %NULL if one is not currently constructed
 * Since: 0.1.0
 */
GBytes *
mwt_tariff_builder_get_tariff_as_bytes (MwtTariffBuilder *self)
{
  g_return_val_if_fail (MWT_IS_TARIFF_BUILDER (self), NULL);

  g_autoptr(GVariant) variant = mwt_tariff_builder_get_tariff_as_variant (self);
  if (variant == NULL)
    return NULL;

  return g_variant_get_data_as_bytes (variant);
}
