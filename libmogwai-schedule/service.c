/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2017, 2018 Endless Mobile, Inc.
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
#include <libmogwai-schedule/clock-system.h>
#include <libmogwai-schedule/connection-monitor-nm.h>
#include <libmogwai-schedule/peer-manager-dbus.h>
#include <libmogwai-schedule/schedule-service.h>
#include <libmogwai-schedule/scheduler.h>
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

static void notify_busy_cb (GObject    *obj,
                            GParamSpec *pspec,
                            gpointer    user_data);

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

  MwsScheduler *scheduler;  /* (owned) */
  MwsScheduleService *schedule_service;  /* (owned) */

  GCancellable *cancellable;  /* (owned) */

  gboolean busy;
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

  if (self->schedule_service != NULL)
    g_signal_handlers_disconnect_by_func (self->schedule_service, notify_busy_cb, self);

  g_clear_object (&self->schedule_service);
  g_clear_object (&self->scheduler);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_service_parent_class)->dispose (object);
}

static void connection_monitor_new_cb (GObject      *source_object,
                                       GAsyncResult *result,
                                       gpointer      user_data);

static void
mws_service_startup_async (HlpService          *service,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (service, cancellable, callback, user_data);
  g_task_set_source_tag (task, mws_service_startup_async);

  /* Get a connection monitor first. */
  mws_connection_monitor_nm_new_async (cancellable, connection_monitor_new_cb,
                                       g_steal_pointer (&task));
}

static void
connection_monitor_new_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  MwsService *self = MWS_SERVICE (g_task_get_source_object (task));
  g_autoptr(GError) local_error = NULL;

  g_autoptr(MwsConnectionMonitor) connection_monitor = NULL;
  connection_monitor = MWS_CONNECTION_MONITOR (mws_connection_monitor_nm_new_finish (result, &local_error));
  if (local_error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  GDBusConnection *connection = hlp_service_get_dbus_connection (HLP_SERVICE (self));

  g_autoptr(MwsPeerManager) peer_manager = NULL;
  peer_manager = MWS_PEER_MANAGER (mws_peer_manager_dbus_new (connection));

  g_autoptr(MwsClock) clock = MWS_CLOCK (mws_clock_system_new ());

  self->scheduler = mws_scheduler_new (connection_monitor, peer_manager, clock);
  self->schedule_service = mws_schedule_service_new (connection,
                                                     "/com/endlessm/DownloadManager1",
                                                     self->scheduler);
  g_signal_connect (self->schedule_service, "notify::busy",
                    (GCallback) notify_busy_cb, self);
  notify_busy_cb (G_OBJECT (self->schedule_service), NULL, self);

  if (!mws_schedule_service_register (self->schedule_service, &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
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
notify_busy_cb (GObject    *obj,
                GParamSpec *pspec,
                gpointer    user_data)
{
  MwsService *self = MWS_SERVICE (user_data);

  gboolean was_busy = self->busy;
  gboolean now_busy = mws_schedule_service_get_busy (self->schedule_service);

  g_debug ("%s: was_busy: %s, now_busy: %s",
           G_STRFUNC, was_busy ? "yes" : "no", now_busy ? "yes" : "no");

  if (was_busy && !now_busy)
    hlp_service_release (HLP_SERVICE (self));
  else if (!was_busy && now_busy)
    hlp_service_hold (HLP_SERVICE (self));

  self->busy = now_busy;
}

static void
mws_service_shutdown (HlpService *service)
{
  MwsService *self = MWS_SERVICE (service);

  g_cancellable_cancel (self->cancellable);
  mws_schedule_service_unregister (self->schedule_service);
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
                       "inactivity-timeout", 30000  /* ms */,
                       "translation-domain", GETTEXT_PACKAGE,
                       "parameter-string", _("— schedule downloads to conserve bandwidth"),
                       "summary", _("Schedule large downloads from multiple "
                                    "system processes to conserve bandwidth "
                                    "and avoid unnecessary use of metered "
                                    "data."),
                       NULL);
}

