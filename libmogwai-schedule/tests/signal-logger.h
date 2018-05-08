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

typedef struct _MwsSignalLoggerEmission MwsSignalLoggerEmission;

void             mws_signal_logger_emission_free       (MwsSignalLoggerEmission *self);
void             mws_signal_logger_emission_get_params (MwsSignalLoggerEmission *self,
                                                        ...);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MwsSignalLoggerEmission, mws_signal_logger_emission_free)

typedef struct _MwsSignalLogger MwsSignalLogger;

MwsSignalLogger *mws_signal_logger_new     (void);
void             mws_signal_logger_free    (MwsSignalLogger *self);
gulong           mws_signal_logger_connect (MwsSignalLogger *self,
                                            gpointer         obj,
                                            const gchar     *signal_name);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MwsSignalLogger, mws_signal_logger_free)

gsize            mws_signal_logger_get_n_emissions  (MwsSignalLogger                *self);
gboolean         mws_signal_logger_pop_emission     (MwsSignalLogger                *self,
                                                     gpointer                       *out_obj,
                                                     gchar                         **out_obj_type_name,
                                                     gchar                         **out_signal_name,
                                                     MwsSignalLoggerEmission       **out_emission);
gchar           *mws_signal_logger_format_emission  (gpointer                        obj,
                                                     const gchar                    *obj_type_name,
                                                     const gchar                    *signal_name,
                                                     const MwsSignalLoggerEmission  *emission);
gchar           *mws_signal_logger_format_emissions (MwsSignalLogger                *self);

/**
 * mws_signal_logger_assert_no_emissions:
 * @self: a #MwsSignalLogger
 *
 * Assert that there are no signal emissions currently in the logged stack.
 *
 * Since: 0.1.0
 */
#define mws_signal_logger_assert_no_emissions(self) \
  G_STMT_START { \
    if (mws_signal_logger_get_n_emissions (self) > 0) \
      { \
        g_autofree gchar *ane_list = mws_signal_logger_format_emissions (self); \
        g_autofree gchar *ane_message = \
            g_strdup_printf ("Expected no signal emissions, but saw %" G_GSIZE_FORMAT "\n:%s", \
                             mws_signal_logger_get_n_emissions (self), \
                             ane_list); \
        g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                             ane_message); \
    }\
  } G_STMT_END

/**
 * mws_signal_logger_assert_emission_pop:
 * @self: a #MwsSignalLogger
 * @obj: a #GObject instance to assert the emission matches
 * @signal_name: signal name to assert the emission matches
 * @...: return locations for the signal parameters
 *
 * Assert that a signal emission can be popped off the log (using
 * mws_signal_logger_pop_emission()) and that it is an emission of @signal_name
 * on @obj. The parameters from the emission will be returned in the return
 * locations given in the varargs, as with mws_signal_logger_get_params().
 *
 * If a signal emission can’t be popped, or if it doesn’t match @signal_name and
 * @obj, an assertion fails, and some debug output is printed.
 *
 * Since: 0.1.0
 */
#define mws_signal_logger_assert_emission_pop(self, obj, signal_name, ...) \
  G_STMT_START { \
    gpointer aep_obj = NULL; \
    g_autofree gchar *aep_obj_type_name = NULL; \
    g_autofree gchar *aep_signal_name = NULL; \
    g_autoptr(MwsSignalLoggerEmission) aep_emission = NULL; \
    if (mws_signal_logger_pop_emission (self, &aep_obj, &aep_obj_type_name, \
                                        &aep_signal_name, \
                                        &aep_emission)) \
      { \
        if (aep_obj == G_OBJECT (obj) && \
            g_str_equal (aep_signal_name, signal_name)) \
          { \
            /* Passed the test! */ \
            mws_signal_logger_emission_get_params (aep_emission, __VA_ARGS__); \
          } \
        else \
          { \
            g_autofree gchar *aep_args = \
                mws_signal_logger_format_emission (aep_obj,\
                                                   aep_obj_type_name,\
                                                   aep_signal_name,\
                                                   aep_emission); \
            g_autofree gchar *aep_message = \
                g_strdup_printf ("Expected emission of %s::%s from %p, but saw: %s", \
                                 G_OBJECT_TYPE_NAME (obj), signal_name, obj, \
                                 aep_args); \
            g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                                 aep_message); \
          } \
      } \
    else \
      { \
        g_autofree gchar *assert_emission_pop_message = \
            g_strdup_printf ("Expected emission of %s::%s from %p, but saw no emissions", \
                             G_OBJECT_TYPE_NAME (obj), signal_name, obj); \
        g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                             assert_emission_pop_message); \
      } \
  } G_STMT_END

G_END_DECLS
