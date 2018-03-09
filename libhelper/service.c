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
#include <libhelper/service.h>
#include <locale.h>
#include <stdio.h>
#include <systemd/sd-daemon.h>


/* These errors do not need to be registered with
 * g_dbus_error_register_error_domain() as they never go over the bus. */
G_DEFINE_QUARK (HlpServiceError, hlp_service_error)

/* A way of automatically removing bus names when going out of scope. */
typedef guint BusNameId;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC (BusNameId, g_bus_unown_name, 0);

static void hlp_service_dispose      (GObject      *object);
static void hlp_service_get_property (GObject      *object,
                                      guint         property_id,
                                      GValue       *value,
                                      GParamSpec   *pspec);
static void hlp_service_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);

static void cancel_inactivity_timeout (HlpService *self);

/**
 * HlpService:
 *
 * A skeleton implementation of a system service, which exposes itself on the
 * bus with a well-known name.
 *
 * It follows the implementation recommendations in `man 7 daemon`.
 *
 * Since: 0.1.0
 */
typedef struct
{
  GPtrArray *option_groups;  /* (owned) (element-type GOptionGroup) */
  gchar *translation_domain;  /* (owned) */
  gchar *parameter_string;  /* (owned) */
  gchar *summary;  /* (owned) */
  GBusType bus_type;
  gchar *service_id;  /* (owned) */

  GCancellable *cancellable;  /* (owned) */
  GDBusConnection *connection;  /* (owned) */
  GError *run_error;  /* (nullable) (owned) */
  gboolean run_exited;
  int run_exit_signal;

  GMainContext *context;  /* (owned) */

  GSource *sigint_source;  /* (owned) (nullable) */
  GSource *sigterm_source;  /* (owned) (nullable) */

  guint inactivity_timeout_ms;  /* 0 indicates no timeout */
  GSource *inactivity_timeout_source;  /* (owned) (nullable) */
  guint hold_count;
} HlpServicePrivate;

typedef enum
{
  PROP_TRANSLATION_DOMAIN = 1,
  PROP_PARAMETER_STRING,
  PROP_SUMMARY,
  PROP_BUS_TYPE,
  PROP_SERVICE_ID,
  PROP_INACTIVITY_TIMEOUT,
} HlpServiceProperty;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (HlpService, hlp_service, G_TYPE_OBJECT)

static void
hlp_service_class_init (HlpServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_INACTIVITY_TIMEOUT + 1] = { NULL, };

  object_class->dispose = hlp_service_dispose;
  object_class->get_property = hlp_service_get_property;
  object_class->set_property = hlp_service_set_property;

  /**
   * HlpService:translation-domain:
   *
   * The gettext translation domain to use for translating command line help.
   * This is typically `GETTEXT_PACKAGE`.
   *
   * Since: 0.1.0
   */
  props[PROP_TRANSLATION_DOMAIN] =
      g_param_spec_string ("translation-domain", "Translation Domain",
                           "The gettext translation domain to use for "
                           "translating command line help.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * HlpService:parameter-string:
   *
   * A string which is displayed on the first line of `--help` output, after the
   * usage summary. It should be a sentence fragment which describes further
   * parameters, or summarises the functionality of the program (after an
   * em-dash).
   *
   * Since: 0.1.0
   */
  props[PROP_PARAMETER_STRING] =
      g_param_spec_string ("parameter-string", "Parameter String",
                           "A string which is displayed on the first line of "
                           "--help output, after the usage summary.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * HlpService:summary:
   *
   * Summary of the service to display as part of the command line help. This
   * should be translated, and be one or more complete sentences.
   *
   * Since: 0.1.0
   */
  props[PROP_SUMMARY] =
      g_param_spec_string ("summary", "Summary",
                           "Summary of the service to display as part of the "
                           "command line help.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * HlpService:bus-type:
   *
   * The type of bus which the service’s well-known name should be exposed on.
   * This can be overridden on the command line.
   *
   * Since: 0.1.0
   */
  props[PROP_BUS_TYPE] =
      g_param_spec_enum ("bus-type", "Bus Type",
                         "The type of bus which the service’s well-known name "
                         "should be exposed on.",
                         G_TYPE_BUS_TYPE,
                         G_BUS_TYPE_SYSTEM,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * HlpService:service-id:
   *
   * The ID of the service, which must be a well-known D-Bus name to uniquely
   * identify the service.
   *
   * Since: 0.1.0
   */
  props[PROP_SERVICE_ID] =
      g_param_spec_string ("service-id", "Service ID",
                           "The ID of the service, which must be well-known "
                           "D-Bus name to uniquely identify the service.",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * HlpService:inactivity-timeout:
   *
   * An inactivity timeout (in ms), after which the service will automatically
   * exit unless its hold count is greater than zero. Increase/Decrease the hold
   * count by calling hlp_service_hold()/hlp_service_release().
   *
   * A timeout of zero means the service will never automatically exit.
   *
   * Since: 0.1.0
   */
  props[PROP_INACTIVITY_TIMEOUT] =
      g_param_spec_uint ("inactivity-timeout", "Inactivity Timeout",
                         "An inactivity timeout (in ms), after which the "
                         "service will automatically exit.",
                         0, G_MAXUINT, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
hlp_service_init (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  priv->cancellable = g_cancellable_new ();
  priv->context = g_main_context_ref_thread_default ();
}

static void
source_destroy_and_unref (GSource *source)
{
  if (source != NULL)
    {
      g_source_destroy (source);
      g_source_unref (source);
    }
}

static void
hlp_service_dispose (GObject *object)
{
  HlpService *self = HLP_SERVICE (object);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  g_clear_pointer (&priv->sigint_source, (GDestroyNotify) source_destroy_and_unref);
  g_clear_pointer (&priv->sigterm_source, (GDestroyNotify) source_destroy_and_unref);

  cancel_inactivity_timeout (self);

  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);

  g_clear_pointer (&priv->option_groups, g_ptr_array_unref);
  g_clear_pointer (&priv->translation_domain, g_free);
  g_clear_pointer (&priv->parameter_string, g_free);
  g_clear_pointer (&priv->summary, g_free);
  g_clear_pointer (&priv->service_id, g_free);
  g_clear_error (&priv->run_error);
  g_clear_object (&priv->connection);
  g_clear_pointer (&priv->context, g_main_context_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (hlp_service_parent_class)->dispose (object);
}

static void
hlp_service_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  HlpService *self = HLP_SERVICE (object);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  switch ((HlpServiceProperty) property_id)
    {
    case PROP_TRANSLATION_DOMAIN:
      g_value_set_string (value, priv->translation_domain);
      break;
    case PROP_PARAMETER_STRING:
      g_value_set_string (value, priv->parameter_string);
      break;
    case PROP_SUMMARY:
      g_value_set_string (value, priv->summary);
      break;
    case PROP_BUS_TYPE:
      g_value_set_enum (value, priv->bus_type);
      break;
    case PROP_SERVICE_ID:
      g_value_set_string (value, priv->service_id);
      break;
    case PROP_INACTIVITY_TIMEOUT:
      g_value_set_uint (value, priv->inactivity_timeout_ms);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
hlp_service_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  HlpService *self = HLP_SERVICE (object);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  switch ((HlpServiceProperty) property_id)
    {
    case PROP_TRANSLATION_DOMAIN:
      /* Construct only. */
      g_assert (priv->translation_domain == NULL);
      priv->translation_domain = g_value_dup_string (value);
      break;
    case PROP_PARAMETER_STRING:
      /* Construct only. */
      g_assert (priv->parameter_string == NULL);
      priv->parameter_string = g_value_dup_string (value);
      break;
    case PROP_SUMMARY:
      /* Construct only. */
      g_assert (priv->summary == NULL);
      priv->summary = g_value_dup_string (value);
      break;
    case PROP_BUS_TYPE:
      /* Construct only. */
      priv->bus_type = g_value_get_enum (value);
      break;
    case PROP_SERVICE_ID:
      /* Construct only. */
      g_assert (priv->service_id == NULL);
      priv->service_id = g_value_dup_string (value);
      break;
    case PROP_INACTIVITY_TIMEOUT:
      hlp_service_set_inactivity_timeout (self, g_value_get_uint (value));
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * hlp_service_add_option_group:
 * @self: a #HlpService
 * @group: (transfer none): an option group to add
 *
 * Add an option group to the command line options. The options in this group
 * will be listed in the help output, and their values will be set when
 * hlp_service_run() is called.
 *
 * This is effectively a wrapper around g_option_context_add_group(), so see the
 * documentation for that for more information.
 *
 * Since: 0.1.0
 */
void
hlp_service_add_option_group (HlpService   *self,
                              GOptionGroup *group)
{
  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (group != NULL);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  if (priv->option_groups == NULL)
    priv->option_groups = g_ptr_array_new_with_free_func ((GDestroyNotify) g_option_group_unref);

  g_ptr_array_add (priv->option_groups, g_option_group_ref (group));
}

static gboolean
signal_sigint_cb (gpointer user_data)
{
  HlpService *self = HLP_SERVICE (user_data);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  hlp_service_exit (self, NULL, SIGINT);

  /* Remove the signal handler so we can re-raise it later without entering a
   * loop. */
  g_clear_pointer (&priv->sigint_source, (GDestroyNotify) source_destroy_and_unref);
  return G_SOURCE_REMOVE;
}

static gboolean
signal_sigterm_cb (gpointer user_data)
{
  HlpService *self = HLP_SERVICE (user_data);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  hlp_service_exit (self, NULL, SIGTERM);

  /* Remove the signal handler so we can re-raise it later without entering a
   * loop. */
  g_clear_pointer (&priv->sigterm_source, (GDestroyNotify) source_destroy_and_unref);
  return G_SOURCE_REMOVE;
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  HlpService *self = HLP_SERVICE (user_data);

  /* Notify systemd we’re ready. */
  sd_notify (0, "READY=1");

  /* Potentially start a timeout to exiting due to inactivity. */
  hlp_service_release (self);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  HlpService *self = HLP_SERVICE (user_data);
  g_autoptr (GError) error = NULL;

  hlp_service_release (self);

  g_set_error (&error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_NAME_UNAVAILABLE,
               _("Lost D-Bus name ‘%s’; exiting."), name);
  hlp_service_exit (self, error, 0);
}

static gboolean
inactivity_timeout_cb (gpointer user_data)
{
  HlpService *self = HLP_SERVICE (user_data);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  if (priv->hold_count == 0)
    {
      g_autoptr (GError) local_error = NULL;
      g_set_error_literal (&local_error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_TIMEOUT,
                           _("Inactivity timeout reached; exiting."));
      hlp_service_exit (self, local_error, 0);
    }

  return G_SOURCE_REMOVE;
}

static void
cancel_inactivity_timeout (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  g_debug ("%s: Cancelling inactivity timeout (was %s)",
           G_STRFUNC, (priv->inactivity_timeout_source != NULL) ? "set" : "unset");

  g_clear_pointer (&priv->inactivity_timeout_source,
                   (GDestroyNotify) source_destroy_and_unref);
}

static void
maybe_schedule_inactivity_timeout (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  g_debug ("%s: Maybe scheduling inactivity timeout, hold_count: %u, inactivity_timeout_ms: %u",
           G_STRFUNC, priv->hold_count, priv->inactivity_timeout_ms);

  if (priv->hold_count == 0)
    {
      cancel_inactivity_timeout (self);

      if (priv->inactivity_timeout_ms != 0)
        {
          g_debug ("%s: Scheduling inactivity timeout", G_STRFUNC);

          if ((priv->inactivity_timeout_ms % 1000) == 0)
            priv->inactivity_timeout_source = g_timeout_source_new_seconds (priv->inactivity_timeout_ms / 1000);
          else
            priv->inactivity_timeout_source = g_timeout_source_new (priv->inactivity_timeout_ms);
          g_source_set_callback (priv->inactivity_timeout_source, inactivity_timeout_cb, self, NULL);
          g_source_attach (priv->inactivity_timeout_source, priv->context);
        }
    }
}

static void
result_cb (GObject      *obj,
           GAsyncResult *result,
           gpointer      user_data)
{
  GAsyncResult **result_out = user_data;

  *result_out = g_object_ref (result);
}

/**
 * hlp_service_run:
 * @self: a #HlpService
 * @argc: number of arguments in @argv
 * @argv: (array length=argc): argument array
 * @error: return location for a #GError
 *
 * Run the service, and return when the process should exit. If it should exit
 * with an error status, @error is set; otherwise it should exit with exit code
 * zero (success).
 *
 * This handles UNIX signals and command line parsing. If you wish to schedule
 * some work to happen asynchronously while hlp_service_run() is running in your
 * `main()` function, use g_idle_add(). This function, like the rest of the
 * library, is not thread-safe.
 *
 * Since: 0.1.0
 */
void
hlp_service_run (HlpService  *self,
                 int          argc,
                 char       **argv,
                 GError     **error)
{
  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (argc > 0);
  g_return_if_fail (argv != NULL);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);
  HlpServiceClass *service_class = HLP_SERVICE_GET_CLASS (self);

  /* Command line parameters. */
  g_autofree gchar *bus_address = NULL;
  gint64 inactivity_timeout_ms = priv->inactivity_timeout_ms;

  const GOptionEntry entries[] =
    {
      { "bus-address", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &bus_address,
        N_("Address of the D-Bus daemon to connect to and own a name on"),
        N_("ADDRESS") },
      { "inactivity-timeout", 't', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT64, &inactivity_timeout_ms,
        N_("Inactivity timeout to wait for before exiting (in milliseconds)"),
        N_("MS") },
      { NULL, },
    };

  /* Localisation */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Ensure we are not running as root — we don’t need those privileges. */
  if (getuid () == 0 || geteuid () == 0)
    {
      g_set_error_literal (error, HLP_SERVICE_ERROR,
                           HLP_SERVICE_ERROR_INVALID_ENVIRONMENT,
                           _("This daemon must not be run as root."));
      return;
    }

  hlp_service_hold (self);

  /* Set up signal handlers. */
  priv->sigint_source = g_unix_signal_source_new (SIGINT);
  g_source_set_callback (priv->sigint_source, signal_sigint_cb, self, NULL);
  g_source_attach (priv->sigint_source, priv->context);
  priv->sigterm_source = g_unix_signal_source_new (SIGTERM);
  g_source_set_callback (priv->sigterm_source, signal_sigterm_cb, self, NULL);
  g_source_attach (priv->sigterm_source, priv->context);

  /* Handle command line parameters. */
  g_autoptr (GOptionContext) context = g_option_context_new (priv->parameter_string);
  g_option_context_set_summary (context, priv->summary);
  g_option_context_add_main_entries (context, entries,
                                     priv->translation_domain);

  if (service_class->get_main_option_entries != NULL)
    {
      GOptionGroup *main_group;
      g_autofree GOptionEntry *main_entries = NULL;

      main_group = g_option_context_get_main_group (context);
      main_entries = service_class->get_main_option_entries (self);
      g_option_group_add_entries (main_group, main_entries);
    }

  for (guint i = 0; priv->option_groups != NULL && i < priv->option_groups->len; i++)
    g_option_context_add_group (context, priv->option_groups->pdata[i]);

  if (priv->option_groups != NULL)
    {
      g_ptr_array_set_free_func (priv->option_groups, NULL);
      g_ptr_array_set_size (priv->option_groups, 0);
    }

  g_autoptr (GError) child_error = NULL;

  if (!g_option_context_parse (context, &argc, &argv, &child_error))
    {
      g_set_error (error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_INVALID_OPTIONS,
                   _("Option parsing failed: %s"), child_error->message);
      hlp_service_release (self);
      return;
    }

  /* Sort out the inactivity timeout. Zero is the default, so ignore that so
   * that subclasses can set their own defaults at construction time. */
  if (inactivity_timeout_ms < 0 || inactivity_timeout_ms > G_MAXUINT)
    {
      g_set_error (error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_INVALID_OPTIONS,
                   _("Invalid inactivity timeout %" G_GINT64_FORMAT "ms."),
                   inactivity_timeout_ms);
      hlp_service_release (self);
      return;
    }
  else if (inactivity_timeout_ms >= 0)
    {
      hlp_service_set_inactivity_timeout (self, inactivity_timeout_ms);
    }

  /* Connect to the bus. */
  if (bus_address == NULL)
    {
      bus_address = g_dbus_address_get_for_bus_sync (priv->bus_type,
                                                     priv->cancellable,
                                                     &child_error);
    }

  if (child_error != NULL)
    {
      g_set_error (error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_NAME_UNAVAILABLE,
                   _("D-Bus unavailable: %s"), child_error->message);
      hlp_service_release (self);
      return;
    }

  g_autoptr (GAsyncResult) connection_result = NULL;
  g_dbus_connection_new_for_address (bus_address,
                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                     NULL  /* observer */,
                                     priv->cancellable,
                                     result_cb,
                                     &connection_result);

  /* Run the main loop until we get a connection or exit. */
  while (connection_result == NULL)
    g_main_context_iteration (NULL, TRUE);

  priv->connection = g_dbus_connection_new_for_address_finish (connection_result,
                                                               &child_error);

  if (priv->connection == NULL)
    {
      g_set_error (error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_NAME_UNAVAILABLE,
                   _("D-Bus bus ‘%s’ unavailable: %s"),
                   bus_address, child_error->message);
      hlp_service_release (self);
      return;
    }

  /* Start up. */
  g_autoptr (GAsyncResult) startup_result = NULL;
  g_assert (service_class->startup_async != NULL &&
            service_class->startup_finish != NULL);
  service_class->startup_async (self, priv->cancellable, result_cb,
                                &startup_result);

  while (startup_result == NULL)
    g_main_context_iteration (NULL, TRUE);

  service_class->startup_finish (self, startup_result, &child_error);

  if (child_error != NULL)
    {
      g_propagate_error (error, child_error);
      hlp_service_release (self);
      return;
    }

  /* Grab a well-known name. */
  g_auto (BusNameId) bus_name_id =
      g_bus_own_name_on_connection (priv->connection,
                                    priv->service_id,
                                    G_BUS_NAME_OWNER_FLAGS_NONE,
                                    name_acquired_cb,
                                    name_lost_cb,
                                    self, NULL);

  /* Run the main loop until stopped from a callback with hlp_service_exit(). */
  while (priv->run_error == NULL && !priv->run_exited)
    g_main_context_iteration (NULL, TRUE);

  hlp_service_hold (self);

  /* Notify systemd we’re shutting down. */
  sd_notify (0, "STOPPING=1");

  /* Debug. */
  g_debug ("Shutting down: cancellable: %s, run_error: %s, run_exited: %s, "
           "run_exit_signal: %d",
           g_cancellable_is_cancelled (priv->cancellable) ? "cancelled" : "no",
           (priv->run_error != NULL) ? "set" : "unset",
           priv->run_exited ? "yes" : "no",
           priv->run_exit_signal);

  /* Shut down. */
  g_assert (service_class->shutdown != NULL);
  service_class->shutdown (self);

  hlp_service_release (self);

  if (priv->run_error != NULL)
    {
      g_propagate_error (error, priv->run_error);
      priv->run_error = NULL;
      return;
    }
}

/**
 * hlp_service_exit:
 * @self: a #HlpService
 * @error: (nullable): error which caused the process to exit, or %NULL for a
 *    successful exit
 * @signum: signal which caused the process to exit, or 0 for a successful exit
 *
 * Cause the service to exit from hlp_service_run(). If @error is non-%NULL, the
 * service will exit with the given error; otherwise it will exit successfully.
 * If this is called multiple times, all errors except the first will be
 * ignored, so it may be safely used for error handling in shutdown code.
 *
 * Since: 0.1.0
 */
void
hlp_service_exit (HlpService   *self,
                  const GError *error,
                  int           signum)
{
  g_autoptr (GError) allocated_error = NULL;

  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (error == NULL || signum == 0);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  if (signum != 0)
    {
      g_assert (error == NULL);

      g_set_error (&allocated_error, HLP_SERVICE_ERROR,
                   HLP_SERVICE_ERROR_SIGNALLED,
                   _("Signalled with signal %d"), signum);
      error = allocated_error;
    }

  if (priv->run_error == NULL)
    {
      if (error != NULL)
        g_debug ("Exiting with error: %s", error->message);
      else
        g_debug ("Exiting with no error");

      if (error != NULL && priv->run_error == NULL)
        priv->run_error = g_error_copy (error);
    }
  else if (error != NULL)
    {
      g_debug ("Ignoring additional error: %s", error->message);
    }

  priv->run_exited = TRUE;
  priv->run_exit_signal = signum;
  g_cancellable_cancel (priv->cancellable);
}

/**
 * hlp_service_get_dbus_connection:
 * @self: a #HlpService
 *
 * Get the #GDBusConnection used to export the service’s well-known name, as
 * specified in #HlpService:bus-type.
 *
 * Returns: (transfer none): D-Bus connection
 * Since: 0.1.0
 */
GDBusConnection *
hlp_service_get_dbus_connection (HlpService *self)
{
  g_return_val_if_fail (HLP_IS_SERVICE (self), NULL);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);
  return priv->connection;
}

/**
 * hlp_service_get_exit_signal:
 * @self: a #HlpService
 *
 * Get the number of the signal which caused the #HlpService to exit.
 *
 * Returns: exit signal number, or 0 if unset
 * Since: 0.1.0
 */
int
hlp_service_get_exit_signal (HlpService *self)
{
  g_return_val_if_fail (HLP_IS_SERVICE (self), 0);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);
  return priv->run_exit_signal;
}

/**
 * hlp_service_get_inactivity_timeout:
 * @self: a #HlpService
 *
 * Get the value of #HlpService:inactivity-timeout.
 *
 * Returns: inactivity timeout, in milliseconds, or zero if inactivity is ignored
 * Since: 0.1.0
 */
guint
hlp_service_get_inactivity_timeout (HlpService *self)
{
  g_return_val_if_fail (HLP_IS_SERVICE (self), 0);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);
  return priv->inactivity_timeout_ms;
}

/**
 * hlp_service_set_inactivity_timeout:
 * @self: a #HlpService
 * @timeout_ms: inactivity timeout (in ms), or zero for no timeout
 *
 * Set the value of #HlpService:inactivity-timeout.
 *
 * Since: 0.1.0
 */
void
hlp_service_set_inactivity_timeout (HlpService *self,
                                    guint       timeout_ms)
{
  g_return_if_fail (HLP_IS_SERVICE (self));

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  if (priv->inactivity_timeout_ms == timeout_ms)
    return;

  priv->inactivity_timeout_ms = timeout_ms;
  g_object_notify (G_OBJECT (self), "inactivity-timeout");

  maybe_schedule_inactivity_timeout (self);
}

/**
 * hlp_service_hold:
 * @self: a #HlpService
 *
 * Increase the hold count of the service, and hence prevent it from
 * automatically exiting after the #HlpService:inactivity-timeout period expires
 * with no activity.
 *
 * Call hlp_service_release() to decrement the hold count. Calls to these two
 * methods must be paired; it is a programmer error not to.
 *
 * Since: 0.1.0
 */
void
hlp_service_hold (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (priv->hold_count < G_MAXUINT);

  priv->hold_count++;
  cancel_inactivity_timeout (self);
}

/**
 * hlp_service_release:
 * @self: a #HlpService
 *
 * Decrease the hold count of the service, and hence potentially (if the hold
 * count reaches zero) allow it to automatically exit after the
 * #HlpService:inactivity-timeout period expires with no activity.
 *
 * Call hlp_service_hold() to increment the hold count. Calls to these two
 * methods must be paired; it is a programmer error not to.
 *
 * Since: 0.1.0
 */
void
hlp_service_release (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (priv->hold_count > 0);

  priv->hold_count--;
  maybe_schedule_inactivity_timeout (self);
}
