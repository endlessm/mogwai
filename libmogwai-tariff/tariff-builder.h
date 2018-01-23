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
#include <libmogwai-tariff/tariff.h>

G_BEGIN_DECLS

#define MWT_TYPE_TARIFF_BUILDER mwt_tariff_builder_get_type ()
G_DECLARE_FINAL_TYPE (MwtTariffBuilder, mwt_tariff_builder, MWT, TARIFF_BUILDER, GObject)

MwtTariffBuilder *mwt_tariff_builder_new                   (void);

void              mwt_tariff_builder_reset                 (MwtTariffBuilder *self);

void              mwt_tariff_builder_set_name              (MwtTariffBuilder *self,
                                                            const gchar      *name);
void              mwt_tariff_builder_add_period            (MwtTariffBuilder *self,
                                                            MwtPeriod        *period);

MwtTariff        *mwt_tariff_builder_get_tariff            (MwtTariffBuilder *self);
GVariant         *mwt_tariff_builder_get_tariff_as_variant (MwtTariffBuilder *self);
GBytes           *mwt_tariff_builder_get_tariff_as_bytes   (MwtTariffBuilder *self);

G_END_DECLS
