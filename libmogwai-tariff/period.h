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

G_BEGIN_DECLS

/**
 * MwtPeriodError:
 * @MWT_PERIOD_ERROR_INVALID: Properties for the #MwtPeriod are invalid.
 *
 * Errors which can be returned by #MwtPeriod.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWT_PERIOD_ERROR_INVALID = 0,
} MwtPeriodError;
#define MWT_PERIOD_N_ERRORS (MWT_PERIOD_ERROR_INVALID + 1)

GQuark mwt_period_error_quark (void);
#define MWT_PERIOD_ERROR mwt_period_error_quark ()

/**
 * MwtPeriodRepeatType:
 * @MWT_PERIOD_REPEAT_NONE: Do not repeat.
 * @MWT_PERIOD_REPEAT_HOUR: Repeat hourly.
 * @MWT_PERIOD_REPEAT_DAY: Repeat daily.
 * @MWT_PERIOD_REPEAT_WEEK: Repeat weekly.
 * @MWT_PERIOD_REPEAT_MONTH: Repeat monthly.
 * @MWT_PERIOD_REPEAT_YEAR: Repeat yearly.
 *
 * Units for calculating with the #MwtPeriod:repeat-period of an #MwtPeriod.
 *
 * Since: 0.1.0
 */
typedef enum
{
  MWT_PERIOD_REPEAT_NONE = 0,
  MWT_PERIOD_REPEAT_HOUR,
  MWT_PERIOD_REPEAT_DAY,
  MWT_PERIOD_REPEAT_WEEK,
  MWT_PERIOD_REPEAT_MONTH,
  MWT_PERIOD_REPEAT_YEAR,
} MwtPeriodRepeatType;

#define MWT_TYPE_PERIOD mwt_period_get_type ()
G_DECLARE_FINAL_TYPE (MwtPeriod, mwt_period, MWT, PERIOD, GObject)

gboolean   mwt_period_validate  (GDateTime            *start,
                                 GDateTime            *end,
                                 MwtPeriodRepeatType   repeat_type,
                                 guint                 repeat_period,
                                 GError              **error);

MwtPeriod *mwt_period_new       (GDateTime            *start,
                                 GDateTime            *end,
                                 MwtPeriodRepeatType   repeat_type,
                                 guint                 repeat_period,
                                 const gchar          *first_property_name,
                                 ...);

GDateTime *mwt_period_get_start (MwtPeriod *self);
GDateTime *mwt_period_get_end   (MwtPeriod *self);

gboolean   mwt_period_contains_time       (MwtPeriod  *self,
                                           GDateTime  *when,
                                           GDateTime **out_start,
                                           GDateTime **out_end);
gboolean   mwt_period_get_next_recurrence (MwtPeriod  *self,
                                           GDateTime  *after,
                                           GDateTime **out_next_start,
                                           GDateTime **out_next_end);

MwtPeriodRepeatType mwt_period_get_repeat_type    (MwtPeriod *self);
guint               mwt_period_get_repeat_period  (MwtPeriod *self);

guint64             mwt_period_get_capacity_limit (MwtPeriod *self);

G_END_DECLS
