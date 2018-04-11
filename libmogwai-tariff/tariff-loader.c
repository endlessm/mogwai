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
#include <libmogwai-tariff/tariff-loader.h>
#include <libmogwai-tariff/tariff.h>
#include <malloc.h>
#include <stdlib.h>


static void mwt_tariff_loader_dispose (GObject *object);

/**
 * MwtTariffLoader:
 *
 * A helper object for loading an #MwtTariff from its serialised form as a
 * #GBytes. See #MwtTariffBuilder for the inverse operation.
 *
 * When using a #MwtTariffLoader, the tariff must be loaded from a #GBytes or
 * a #GVariant using mwt_tariff_loader_load_from_bytes() or
 * mwt_tariff_loader_load_from_variant(). If that succeeds, the #MwtTariff
 * object may be retrieved using mwt_tariff_loader_get_tariff(). This will be
 * %NULL if loading failed. Details of the failure will come from the #GError
 * set by the loading function.
 *
 * A #MwtTariffLoader can be reused to load multiple tariffs. Subsequent calls
 * to the loading functions will clear any previously loaded tariff on success
 * or failure.
 *
 * Since: 0.1.0
 */
struct _MwtTariffLoader
{
  GObject parent;

  MwtTariff *final_tariff;  /* (nullable) (owned) */
};

G_DEFINE_TYPE (MwtTariffLoader, mwt_tariff_loader, G_TYPE_OBJECT)

static void
mwt_tariff_loader_class_init (MwtTariffLoaderClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = mwt_tariff_loader_dispose;
}

static void
mwt_tariff_loader_init (MwtTariffLoader *self)
{
  /* Nothing to do here. */
}

static void
mwt_tariff_loader_dispose (GObject *object)
{
  MwtTariffLoader *self = MWT_TARIFF_LOADER (object);

  g_clear_object (&self->final_tariff);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mwt_tariff_loader_parent_class)->dispose (object);
}

/**
 * mwt_tariff_loader_new:
 *
 * Create a new, empty #MwtTariffLoader.
 *
 * Returns: (transfer full): a new #MwtTariffLoader
 * Since: 0.1.0
 */
MwtTariffLoader *
mwt_tariff_loader_new (void)
{
  return g_object_new (MWT_TYPE_TARIFF_LOADER, NULL);
}

/* Allocate an 8-aligned block of @size bytes of memory.
 * FIXME: This should not be necessary; g_variant_new_from_bytes() should do
 * this for us. */
static gpointer
align_malloc (gsize size)
{
  gpointer mem;

  if (posix_memalign (&mem, 8, size))
    g_error ("posix_memalign failed");

  return mem;
}

static void
align_free (gpointer mem)
{
  free (mem);
}

/**
 * mwt_tariff_loader_load_from_bytes:
 * @self: a #MwtTariffLoader
 * @bytes: the data to load
 * @error: return location for a #GError, or %NULL
 *
 * Try to load a tariff from its serialised form in @bytes. The data in @bytes
 * must be exactly as produced by a #MwtTariffBuilder, without any additional
 * byte swapping or zero padding.
 *
 * On success, the loaded tariff will be available by calling
 * mwt_tariff_loader_get_tariff().
 *
 * Note: @bytes must be backed by memory that is at least 8-byte aligned (even
 * on platforms with narrower native alignment). Otherwise, this function will
 * internally create a copy of the memory.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_tariff_loader_load_from_bytes (MwtTariffLoader  *self,
                                   GBytes           *bytes,
                                   GError          **error)
{
  g_return_val_if_fail (MWT_IS_TARIFF_LOADER (self), FALSE);
  g_return_val_if_fail (bytes != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Clear any existing result. */
  g_clear_object (&self->final_tariff);

  /* FIXME: GLib really should handle this itself. We need to 8-align as the
   * type of our variant is (sqv), and `v` must be 8-aligned. */
  g_autoptr(GBytes) aligned_bytes = NULL;
  gsize unaligned_data_len;
  gconstpointer unaligned_data = g_bytes_get_data (bytes, &unaligned_data_len);
  if (((guintptr) unaligned_data) % 8 != 0)
    {
      guint8 *aligned_data = align_malloc (unaligned_data_len);
      memcpy (aligned_data, unaligned_data, unaligned_data_len);
      aligned_bytes = g_bytes_new_with_free_func (aligned_data, unaligned_data_len,
                                                  align_free, NULL);
    }
  else
    aligned_bytes = g_bytes_ref (bytes);

  g_autoptr(GVariant) variant = NULL;
  variant = g_variant_new_from_bytes (G_VARIANT_TYPE ("(sqv)"), aligned_bytes, FALSE);

  return mwt_tariff_loader_load_from_variant (self, variant, error);
}

/* Construct a #GDateTime from the given @unix_timestamp (always in UTC) and
 * @timezone_identifier (for example, ‘Europe/London’; note that this is *not*
 * a timezone abbreviation like ‘AST’). This will return %NULL if an invalid
 * time results. */
static GDateTime *
date_time_new_from_unix (guint64      unix_timestamp,
                         const gchar *timezone_identifier)
{
  g_autoptr(GDateTime) utc = g_date_time_new_from_unix_utc (unix_timestamp);
  if (utc == NULL)
    return NULL;

  g_autoptr(GTimeZone) tz = NULL;
  if (*timezone_identifier == '\0')
    tz = g_time_zone_new_local ();
  else
    tz = g_time_zone_new (timezone_identifier);

  g_debug ("%s: Created timezone ‘%s’ for ‘%s’, with offset %d at interval 0",
           G_STRFUNC, g_time_zone_get_identifier (tz), timezone_identifier,
           g_time_zone_get_offset (tz, 0));

  /* Creating a timezone can’t actually fail, but if we fail to load the
   * timezone information, the #GTimeZone will just represent UTC. Catch that. */
  g_assert (tz != NULL);
  if (!g_str_equal (g_time_zone_get_identifier (tz), timezone_identifier))
    return NULL;

  return g_date_time_to_timezone (utc, tz);
}

/**
 * mwt_tariff_loader_load_from_variant:
 * @self: a #MwtTariffLoader
 * @variant: the variant to load
 * @error: return location for a #GError, or %NULL
 *
 * Version of mwt_tariff_loader_load_from_bytes() which loads from a
 * deserialised #GVariant. mwt_tariff_loader_load_from_bytes() is essentially a
 * wrapper around g_variant_new_from_bytes() and this function.
 *
 * It is not a programming error if the given @variant is not in normal form,
 * or is of the wrong type.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mwt_tariff_loader_load_from_variant (MwtTariffLoader  *self,
                                     GVariant         *variant,
                                     GError          **error)
{
  g_return_val_if_fail (MWT_IS_TARIFF_LOADER (self), FALSE);
  g_return_val_if_fail (variant != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Clear any existing result. */
  g_clear_object (&self->final_tariff);

  /* This also checks whether the inner variant is in normal form. */
  if (!g_variant_is_normal_form (variant))
    {
      g_set_error_literal (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                           _("Input data is not in normal form."));
      return FALSE;
    }

  guint16 format_version;
  const gchar *format_magic;
  g_autoptr(GVariant) inner_variant = NULL;
  g_variant_get (variant, "(&sqv)",
                 &format_magic, &format_version, &inner_variant);

  /* Check the magic first. */
  if (!g_str_equal (format_magic, "Mogwai tariff"))
    {
      g_set_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                   _("Unknown file format magic ‘%s’."), format_magic);
      return FALSE;
    }

  /* Is the version number byteswapped? It should be 0x0001. */
  if (format_version == 0x0100 || format_version == 0x0200)
    {
      /* FIXME: Mess around with refs because g_variant_byteswap() had a bug
       * with how it handled floating refs.
       * See: https://bugzilla.gnome.org/show_bug.cgi?id=792612 */
      g_autoptr(GVariant) inner_variant_swapped = NULL;
      inner_variant_swapped = g_variant_byteswap (inner_variant);
      g_variant_unref (inner_variant);
      inner_variant = g_steal_pointer (&inner_variant_swapped);
      format_version = GUINT16_SWAP_LE_BE (format_version);
    }
  else if (format_version != 0x0001 && format_version != 0x0002)
    {
      g_set_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                   _("Unknown file format version %x02."),
                   format_version);
      return FALSE;
    }

  /* Load version 1 of the file format. */
  if ((format_version == 1 &&
       !g_variant_is_of_type (inner_variant, G_VARIANT_TYPE ("(sa(ttqut))"))) ||
      (format_version == 2 &&
       !g_variant_is_of_type (inner_variant, G_VARIANT_TYPE ("(sa(ttssqut))"))))
    {
      g_set_error (error, MWT_TARIFF_ERROR, MWT_TARIFF_ERROR_INVALID,
                   _("Input data does not have correct type."));
      return FALSE;
    }

  const gchar *name;
  g_autoptr(GVariantIter) iter = NULL;
  g_variant_get (inner_variant, "(&sa*)", &name, &iter);

  guint64 start_unix, end_unix, capacity_limit;
  const gchar *start_timezone, *end_timezone;
  guint16 repeat_type_uint16;
  guint32 repeat_period;

  g_autoptr(GPtrArray) periods = g_ptr_array_new_with_free_func (g_object_unref);
  gsize i = 0;

  while (i++,
         (format_version == 2) ?
             g_variant_iter_loop (iter, "(tt&s&squt)",
                                  &start_unix,
                                  &end_unix,
                                  &start_timezone,
                                  &end_timezone,
                                  &repeat_type_uint16,
                                  &repeat_period,
                                  &capacity_limit) :
             g_variant_iter_loop (iter, "(ttqut)",
                                  &start_unix,
                                  &end_unix,
                                  &repeat_type_uint16,
                                  &repeat_period,
                                  &capacity_limit))
    {
      /* Version 1 only supported UTC timezones. */
      if (format_version == 1)
        {
          start_timezone = "Z";
          end_timezone = "Z";
        }

      /* Note: @start and @end might be %NULL. mwt_period_validate() handles that. */
      g_autoptr(GDateTime) start = date_time_new_from_unix (start_unix, start_timezone);
      g_autoptr(GDateTime) end = date_time_new_from_unix (end_unix, end_timezone);
      MwtPeriodRepeatType repeat_type = (MwtPeriodRepeatType) repeat_type_uint16;

      g_autoptr(GError) local_error = NULL;
      if (!mwt_period_validate (start, end, repeat_type, repeat_period, &local_error))
        {
          g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                      _("Error parsing period %" G_GSIZE_FORMAT ": "),
                                      i);
          return FALSE;
        }

      g_autoptr(MwtPeriod) period = NULL;
      period = mwt_period_new (start, end, repeat_type, repeat_period,
                               "capacity-limit", capacity_limit,
                               NULL);
      g_ptr_array_add (periods, g_steal_pointer (&period));
    }

  g_autoptr(GError) local_error = NULL;
  if (!mwt_tariff_validate (name, periods, &local_error))
    {
      g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                  _("Error parsing tariff: "));
      return FALSE;
    }

  self->final_tariff = mwt_tariff_new (name, periods);

  return TRUE;
}

/**
 * mwt_tariff_loader_get_tariff:
 * @self: a #MwtTariffLoader
 *
 * Get the loaded #MwtTariff, or %NULL if nothing has been loaded yet or if
 * loading the tariff failed.
 *
 * Returns: (transfer none) (nullable): the loaded #MwtTariff, or %NULL
 * Since: 0.1.0
 */
MwtTariff *
mwt_tariff_loader_get_tariff (MwtTariffLoader *self)
{
  g_return_val_if_fail (MWT_IS_TARIFF_LOADER (self), NULL);

  return self->final_tariff;
}
