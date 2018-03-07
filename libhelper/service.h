/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017 Endless Mobile, Inc.
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

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * HLP_SERVICE_ERROR:
 *
 * Error domain for #HlpServiceError.
 *
 * Since: 0.1.0
 */
#define HLP_SERVICE_ERROR hlp_service_error_quark ()
GQuark hlp_service_error_quark (void);

/**
 * HlpServiceError:
 * @HLP_SERVICE_ERROR_SIGNALLED: Process was signalled with `SIGINT` or
 *    `SIGTERM`.
 * @HLP_SERVICE_ERROR_INVALID_OPTIONS: Invalid command line options.
 * @HLP_SERVICE_ERROR_NAME_UNAVAILABLE: Bus or well-known name unavailable.
 * @HLP_SERVICE_ERROR_INVALID_ENVIRONMENT: Runtime environment is insecure or
 *    otherwise invalid for running the daemon.
 * @HLP_SERVICE_ERROR_TIMEOUT: Inactivity timeout reached.
 *
 * Errors from running a service.
 *
 * Since: 0.1.0
 */
typedef enum
{
  HLP_SERVICE_ERROR_SIGNALLED,
  HLP_SERVICE_ERROR_INVALID_OPTIONS,
  HLP_SERVICE_ERROR_NAME_UNAVAILABLE,
  HLP_SERVICE_ERROR_INVALID_ENVIRONMENT,
  HLP_SERVICE_ERROR_TIMEOUT,
} HlpServiceError;

#define HLP_TYPE_SERVICE hlp_service_get_type ()
G_DECLARE_DERIVABLE_TYPE (HlpService, hlp_service, HLP, SERVICE, GObject)

struct _HlpServiceClass
{
  GObjectClass parent_class;

  GOptionEntry *(*get_main_option_entries) (HlpService *service);

  void (*startup_async)  (HlpService          *service,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data);
  void (*startup_finish) (HlpService          *service,
                          GAsyncResult        *result,
                          GError             **error);
  void (*shutdown)       (HlpService          *service);

  gpointer padding[12];
};

void        hlp_service_add_option_group (HlpService    *self,
                                          GOptionGroup  *group);

void        hlp_service_run              (HlpService    *self,
                                          int            argc,
                                          char         **argv,
                                          GError       **error);
void        hlp_service_exit             (HlpService    *self,
                                          const GError  *error,
                                          int            signum);

GDBusConnection *hlp_service_get_dbus_connection (HlpService *self);
int              hlp_service_get_exit_signal     (HlpService *self);

guint hlp_service_get_inactivity_timeout (HlpService *self);
void  hlp_service_set_inactivity_timeout (HlpService *self,
                                          guint       timeout_ms);

void  hlp_service_hold    (HlpService *self);
void  hlp_service_release (HlpService *self);

G_END_DECLS
