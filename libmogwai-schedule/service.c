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

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/service.h>
#include <locale.h>


static void mws_service_dispose (GObject *object);

static void mws_service_startup_async (HlpService          *service,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
static void mws_service_startup_finish (HlpService    *service,
                                        GAsyncResult  *result,
                                        GError       **error);
static void mws_service_shutdown (HlpService *service);

/**
 * MwsService:
 *
 * The core implementation of the scheduling daemon, which exposes its D-Bus
 * API on the bus.
 *
 * Since: 0.1.0
 */
struct _MwsService
{
  HlpService parent;

  GCancellable *cancellable;  /* owned */
};

G_DEFINE_TYPE (MwsService, mws_service, HLP_TYPE_SERVICE)

static void
mws_service_class_init (MwsServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  HlpServiceClass *service_class = (HlpServiceClass *) klass;

  object_class->dispose = mws_service_dispose;

  service_class->startup_async = mws_service_startup_async;
  service_class->startup_finish = mws_service_startup_finish;
  service_class->shutdown = mws_service_shutdown;
}

static void
mws_service_init (MwsService *self)
{
  /* A cancellable to allow pending asynchronous operations to be cancelled when
   * shutdown() is called. */
  self->cancellable = g_cancellable_new ();
}

static void
mws_service_dispose (GObject *object)
{
  MwsService *self = MWS_SERVICE (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_service_parent_class)->dispose (object);
}

static void
mws_service_startup_async (HlpService          *service,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  GDBusConnection *connection;

  task = g_task_new (service, cancellable, callback, user_data);
  g_task_set_source_tag (task, mws_service_startup_async);

  connection = hlp_service_get_dbus_connection (service);

  /* TODO: Create our D-Bus API here. */

  g_task_return_boolean (task, TRUE);
}

static void
mws_service_startup_finish (HlpService    *service,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_task_propagate_boolean (G_TASK (result), error);
}

static void
mws_service_shutdown (HlpService *service)
{
  MwsService *self = MWS_SERVICE (service);

  g_cancellable_cancel (self->cancellable);
}

/**
 * mws_service_new:
 *
 * Create a new #MwsService.
 *
 * Returns: (transfer full): a new #MwsService
 * Since: 0.1.0
 */
MwsService *
mws_service_new (void)
{
  return g_object_new (MWS_TYPE_SERVICE,
                       "bus-type", G_BUS_TYPE_SYSTEM,
                       "service-id", "com.endlessm.MogwaiSchedule1",
                       "translation-domain", GETTEXT_PACKAGE,
                       "parameter-string", _("— schedule downloads to conserve bandwidth"),
                       "summary", _("Schedule large downloads from multiple "
                                    "system processes to conserve bandwidth "
                                    "and avoid unnecessary use of metered "
                                    "data."),
                       NULL);
}

