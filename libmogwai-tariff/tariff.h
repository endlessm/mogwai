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
#include <libmogwai-tariff/period.h>

G_BEGIN_DECLS

/**
 * MwtTariffError:
 * @MWT_TARIFF_ERROR_INVALID: Properties for the #MwtTariff are invalid.
 *
 * Errors which can be returned by #MwtTariff.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWT_TARIFF_ERROR_INVALID = 0,
} MwtTariffError;
#define MWT_TARIFF_N_ERRORS (MWT_TARIFF_ERROR_INVALID + 1)

GQuark mwt_tariff_error_quark (void);
#define MWT_TARIFF_ERROR mwt_tariff_error_quark ()

#define MWT_TYPE_TARIFF mwt_tariff_get_type ()
G_DECLARE_FINAL_TYPE (MwtTariff, mwt_tariff, MWT, TARIFF, GObject)

gboolean     mwt_tariff_validate         (const gchar  *name,
                                          GPtrArray    *periods,
                                          GError      **error);

MwtTariff   *mwt_tariff_new              (const gchar  *name,
                                          GPtrArray    *periods);

const gchar *mwt_tariff_get_name         (MwtTariff    *self);
GPtrArray   *mwt_tariff_get_periods      (MwtTariff    *self);

MwtPeriod   *mwt_tariff_lookup_period    (MwtTariff    *self,
                                          GDateTime    *when);

gboolean     mwt_tariff_validate_name    (const gchar  *name);

G_END_DECLS
