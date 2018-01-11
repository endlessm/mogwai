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

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <libsoup/soup.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>


/* FIXME: Add tests for this client. */

/* Exit statuses. */
typedef enum
{
  /* Success. */
  EXIT_OK = 0,
  /* Error parsing command line options. */
  EXIT_INVALID_OPTIONS = 1,
  /* Couldn’t connect to D-Bus. */
  EXIT_BUS_UNAVAILABLE = 2,
  /* Command failed. */
  EXIT_FAILED = 3,
} ExitStatus;

/* Main function stuff. */
typedef struct
{
  GCancellable *cancellable;  /* (owned) */
  gint *signum_out;
  guint handler_id;
} SignalData;

static void
signal_data_clear (SignalData *data)
{
  g_clear_object (&data->cancellable);
  /* FIXME: Use g_clear_handle() in future */
  if (data->handler_id != 0)
    {
      g_source_remove (data->handler_id);
      data->handler_id = 0;
    }
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SignalData, signal_data_clear)

static gboolean
handle_signal (SignalData *signal_data,
               gint        signum)
{
  *signal_data->signum_out = signum;
  g_cancellable_cancel (signal_data->cancellable);

  /* Remove the signal handler so when we raise again later, we don’t enter a
   * loop. */
  signal_data->handler_id = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
signal_sigint_cb (gpointer user_data)
{
  SignalData *signal_data = user_data;

  return handle_signal (signal_data, SIGINT);
}

static gboolean
signal_sigterm_cb (gpointer user_data)
{
  SignalData *signal_data = user_data;

  return handle_signal (signal_data, SIGTERM);
}

/* Download handling. */
typedef struct
{
  GDBusConnection *connection;  /* (owned) */
  gchar *uri;  /* (owned) */
  GFile *destination_file;  /* (owned) */
  gboolean is_interactive;
  SoupSession *session;  /* (owned) */
  SoupRequest *request;  /* (owned) (nullable) */
  GDBusProxy *entry_proxy;  /* (owned) (nullable) */
  gulong properties_changed_id;
  GInputStream *request_stream;  /* (owned) (nullable) */
  gsize bytes_spliced;
} DownloadData;

static void
download_data_free (DownloadData *data)
{
  g_clear_object (&data->request_stream);
  if (data->entry_proxy != NULL && data->properties_changed_id != 0)
    g_signal_handler_disconnect (data->entry_proxy, data->properties_changed_id);
  g_clear_object (&data->entry_proxy);
  g_clear_object (&data->request);
  g_clear_object (&data->session);
  g_clear_object (&data->destination_file);
  g_clear_pointer (&data->uri, g_free);
  g_clear_object (&data->connection);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DownloadData, download_data_free);

static void schedule_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data);
static void proxy_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data);
static void entry_proxy_properties_changed_cb (GDBusProxy *proxy,
                                               GVariant   *changed_properties,
                                               GStrv       invalidated_properties,
                                               gpointer    user_data);
static void start_download (GTask *task);
static void request_send_cb (GObject      *obj,
                             GAsyncResult *result,
                             gpointer      user_data);
static void replace_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data);
static void splice_cb (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data);
static void remove_cb (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data);

static void
download_uri_async (const gchar         *uri,
                    GFile               *destination_file,
                    guint32              priority,
                    gboolean             resumable,
                    GDBusConnection     *connection,
                    gboolean             is_interactive,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, download_uri_async);

  g_autoptr(DownloadData) data = g_new0 (DownloadData, 1);
  data->connection = g_object_ref (connection);
  data->uri = g_strdup (uri);
  data->destination_file = g_object_ref (destination_file);
  data->is_interactive = is_interactive;
  data->session = soup_session_new ();

  g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) download_data_free);

  /* Sort out the arguments for the schedule entry. */
  g_auto(GVariantDict) dict;
  g_variant_dict_init (&dict, NULL);
  g_variant_dict_insert (&dict, "Priority", "u", priority);
  g_variant_dict_insert (&dict, "Resumable", "b", resumable);

  /* Create a schedule entry for the download.
   * FIXME: For the moment, we just make the D-Bus calls manually. In future,
   * this will probably be split out into libmogwai-schedule-client. */
  g_dbus_connection_call (connection, "com.endlessm.MogwaiSchedule1",
                          "/com/endlessm/DownloadManager1",
                          "com.endlessm.DownloadManager1.Scheduler",
                          "Schedule",
                          g_variant_new ("(@a{sv})", g_variant_dict_end (&dict)),
                          G_VARIANT_TYPE ("(o)"),
                          G_DBUS_CALL_FLAGS_NONE |
                          (is_interactive ? G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION : 0),
                          -1,  /* default timeout */
                          cancellable,
                          schedule_cb,
                          g_steal_pointer (&task));
}

static gboolean
download_uri_finish (GAsyncResult  *result,
                     GError       **error)
{
  g_assert (g_task_is_valid (result, NULL));
  g_assert (g_async_result_is_tagged (result, download_uri_async));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
schedule_cb (GObject      *obj,
             GAsyncResult *result,
             gpointer      user_data)
{
  GDBusConnection *connection = G_DBUS_CONNECTION (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  /* Grab the schedule entry. */
  g_autoptr(GVariant) return_value = NULL;
  return_value = g_dbus_connection_call_finish (connection, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  const gchar *schedule_entry_path;
  g_variant_get (return_value, "(&o)", &schedule_entry_path);

  g_dbus_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE,
                    NULL  /* interface info */,
                    "com.endlessm.MogwaiSchedule1",
                    schedule_entry_path,
                    "com.endlessm.DownloadManager1.ScheduleEntry",
                    cancellable,
                    proxy_cb,
                    g_steal_pointer (&task));
}

static void
proxy_cb (GObject      *obj,
          GAsyncResult *result,
          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  data->entry_proxy = g_dbus_proxy_new_finish (result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_autoptr(GVariant) download_now_variant = NULL;
  download_now_variant = g_dbus_proxy_get_cached_property (data->entry_proxy,
                                                           "DownloadNow");

  if (download_now_variant == NULL ||
      !g_variant_get_boolean (download_now_variant))
    {
      data->properties_changed_id =
          g_signal_connect (data->entry_proxy, "g-properties-changed",
                            (GCallback) entry_proxy_properties_changed_cb,
                            g_steal_pointer (&task));
    }
  else
    {
      start_download (task);
    }
}

static void
entry_proxy_properties_changed_cb (GDBusProxy *proxy,
                                   GVariant   *changed_properties,
                                   GStrv       invalidated_properties,
                                   gpointer    user_data)
{
  /* The reference counting here is a little unusual: this signal handler
   * closure owns the last reference to the #GTask. Only once the download is
   * started (below), the signal handler is removed and our reference is dropped
   * (but not before start_download() has taken its own reference). */
  GTask *task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);

  /* Don’t bother checking whether DownloadNow was actually changed: it’s
   * cheap enough to just check its latest value.
   *
   * FIXME: In future, we should support DownloadNow flipping value several
   * times over the lifetime of the download, as the scheduler makes this entry
   * active and inactive in order to best use bandwidth. */
  g_autoptr(GVariant) download_now_variant = NULL;
  download_now_variant = g_dbus_proxy_get_cached_property (proxy, "DownloadNow");

  if (download_now_variant != NULL &&
      g_variant_get_boolean (download_now_variant))
    {
      start_download (task);
      g_signal_handler_disconnect (data->entry_proxy, data->properties_changed_id);
      data->properties_changed_id = 0;
      g_object_unref (task);
    }
}

static void
start_download (GTask *task)
{
  DownloadData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  /* Start the download. */
  data->request = soup_session_request (data->session, data->uri, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  soup_request_send_async (data->request, cancellable, request_send_cb, g_object_ref (task));
}

static void
request_send_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  SoupRequest *request = SOUP_REQUEST (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  data->request_stream = soup_request_send_finish (request, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Open the local output file and splice to it. */
  g_file_replace_async (data->destination_file, NULL,
                        FALSE,  /* no backup */
                        G_FILE_CREATE_NONE,
                        G_PRIORITY_DEFAULT,
                        cancellable,
                        replace_cb,
                        g_steal_pointer (&task));
}

static void
replace_cb (GObject      *obj,
            GAsyncResult *result,
            gpointer      user_data)
{
  GFile *destination_file = G_FILE (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  g_autoptr(GFileOutputStream) output_stream = NULL;
  output_stream = g_file_replace_finish (destination_file, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_output_stream_splice_async (G_OUTPUT_STREAM (output_stream),
                                data->request_stream,
                                G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                splice_cb,
                                g_steal_pointer (&task));
}

static void
splice_cb (GObject      *obj,
           GAsyncResult *result,
           gpointer      user_data)
{
  GOutputStream *output_stream = G_OUTPUT_STREAM (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  gssize bytes_spliced = g_output_stream_splice_finish (output_stream, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_assert (bytes_spliced >= 0);
  data->bytes_spliced = (gsize) bytes_spliced;

  /* Now notify the scheduler that the download is complete.
   * Really, we should do this on all the error paths above, but we rely on
   * the scheduler noticing when this process exits. Let’s be explicit in this
   * case to test both code paths in the scheduler. */
  g_dbus_proxy_call (data->entry_proxy,
                     "Remove",
                     NULL,  /* no parameters */
                     G_DBUS_CALL_FLAGS_NONE |
                     (data->is_interactive ? G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION : 0),
                     -1,  /* default timeout */
                     cancellable,
                     remove_cb,
                     g_steal_pointer (&task));
}

static void
remove_cb (GObject      *obj,
           GAsyncResult *result,
           gpointer      user_data)
{
  GDBusProxy *entry_proxy = G_DBUS_PROXY (obj);
  g_autoptr(GTask) task = G_TASK (user_data);
  DownloadData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  g_autoptr(GVariant) reply_variant = NULL;
  reply_variant = g_dbus_proxy_call_finish (entry_proxy, result, &error);

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Success. */
  g_task_return_int (task, data->bytes_spliced);
}

static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GError) error = NULL;

  /* Localisation */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Set up signal handlers. */
  g_autoptr(GCancellable) cancellable = g_cancellable_new ();
  gint signum = 0;

  g_auto(SignalData) sigint_data = { NULL, &signum, 0 };
  sigint_data.cancellable = g_object_ref (cancellable);
  sigint_data.handler_id = g_unix_signal_add (SIGINT, signal_sigint_cb, &sigint_data);

  g_auto(SignalData) sigterm_data = { NULL, &signum, 0 };
  sigterm_data.cancellable = g_object_ref (cancellable);
  sigterm_data.handler_id = g_unix_signal_add (SIGTERM, signal_sigterm_cb, &sigterm_data);

  /* Handle command line parameters. */
  g_autofree gchar *bus_address = NULL;
  gint priority = 0;
  gboolean resumable = FALSE;
  g_auto (GStrv) args = NULL;

  const GOptionEntry entries[] =
    {
      { "bus-address", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &bus_address,
        N_("Address of the D-Bus daemon to connect to (default: system bus)"),
        N_("ADDRESS") },
      { "priority", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &priority,
        N_("Priority of the download, where higher numbers are more important (default: 0)"),
        N_("PRIORITY") },
      { "resumable", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &resumable,
        N_("Enable resume support for this download (default: non-resumable)"),
        NULL },
      { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY,
        &args, NULL, NULL },
      { NULL, },
    };

  g_autoptr(GOptionContext) context = NULL;
  context = g_option_context_new (_("URI OUTPUT-FILENAME"));
  g_option_context_set_summary (context, _("Schedule and download a large file"));
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 error->message);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  if (args == NULL || args[0] == NULL || args[1] == NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 _("A URI and OUTPUT-FILENAME are required"));
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  if (priority < 0 || (guint) priority > G_MAXUINT32)
    {
      g_autofree gchar *message = NULL, *submessage = NULL;
      submessage = g_strdup_printf (_("--priority must be in range [%u, %u]"),
                                    (guint) 0, G_MAXUINT32);
      message = g_strdup_printf (_("Option parsing failed: %s"), submessage);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  const gchar *uri = args[0];
  const gchar *output_filename = args[1];
  g_autoptr(GFile) destination_file = g_file_new_for_commandline_arg (output_filename);

  /* Is this an interactive shell? */
  gboolean is_interactive = isatty (fileno (stdin));

  /* Connect to D-Bus. If no address was specified on the command line, use the
   * system bus. */
  if (bus_address == NULL)
    {
      bus_address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     cancellable, &error);
    }

  if (error != NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("D-Bus system bus unavailable: %s"),
                                 error->message);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_BUS_UNAVAILABLE;
    }

  g_autoptr(GDBusConnection) connection = NULL;
  connection = g_dbus_connection_new_for_address_sync (bus_address,
                                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                       G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                       NULL  /* observer */,
                                                       cancellable, &error);

  if (error != NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("D-Bus bus ‘%s’ unavailable: %s"),
                                 bus_address, error->message);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_BUS_UNAVAILABLE;
    }

  /* Create a #GTask for the scheduling and download, and start downloading. */
  g_autoptr(GAsyncResult) download_result = NULL;
  download_uri_async (uri, destination_file, priority, resumable,
                      connection, is_interactive,
                      cancellable, async_result_cb, &download_result);

  /* Run the main loop until we are signalled or the download finishes. */
  while (!g_cancellable_is_cancelled (cancellable) && download_result == NULL)
    g_main_context_iteration (NULL, TRUE);

  if (download_result != NULL)
    {
      /* Handle errors from the command. */
      download_uri_finish (download_result, &error);

      if (error != NULL)
        {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
              signum != 0)
            raise (signum);

          g_printerr ("%s: %s\n", argv[0], error->message);

          return EXIT_FAILED;
        }
    }

  return EXIT_OK;
}
