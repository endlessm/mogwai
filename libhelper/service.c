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
GQuark
hlp_service_error_quark (void)
{
  return g_quark_from_static_string ("hlp-service-error-quark");
}

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

  gint sigint_id;
  gint sigterm_id;
} HlpServicePrivate;

typedef enum
{
  PROP_TRANSLATION_DOMAIN = 1,
  PROP_PARAMETER_STRING,
  PROP_SUMMARY,
  PROP_BUS_TYPE,
  PROP_SERVICE_ID,
} HlpServiceProperty;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (HlpService, hlp_service, G_TYPE_OBJECT)

static void
hlp_service_class_init (HlpServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_SERVICE_ID + 1] = { NULL, };

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

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
hlp_service_init (HlpService *self)
{
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  priv->cancellable = g_cancellable_new ();
}

static void
hlp_service_dispose (GObject *object)
{
  HlpService *self = HLP_SERVICE (object);
  HlpServicePrivate *priv = hlp_service_get_instance_private (self);

  if (priv->sigint_id != 0)
    {
      g_source_remove (priv->sigint_id);
      priv->sigint_id = 0;
    }

  if (priv->sigterm_id != 0)
    {
      g_source_remove (priv->sigterm_id);
      priv->sigterm_id = 0;
    }

  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);

  g_clear_pointer (&priv->option_groups, g_ptr_array_unref);
  g_clear_pointer (&priv->translation_domain, g_free);
  g_clear_pointer (&priv->parameter_string, g_free);
  g_clear_pointer (&priv->summary, g_free);
  g_clear_pointer (&priv->service_id, g_free);
  g_clear_error (&priv->run_error);
  g_clear_object (&priv->connection);

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
  priv->sigint_id = 0;
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
  priv->sigterm_id = 0;
  return G_SOURCE_REMOVE;
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  /* Notify systemd we’re ready. */
  sd_notify (0, "READY=1");
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  HlpService *self = HLP_SERVICE (user_data);
  g_autoptr (GError) error = NULL;

  g_set_error (&error, HLP_SERVICE_ERROR, HLP_SERVICE_ERROR_NAME_UNAVAILABLE,
               _("Lost D-Bus name ‘%s’; exiting."), name);
  hlp_service_exit (self, error, 0);
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
  /* Command line parameters. */
  g_autofree gchar *bus_address = NULL;

  const GOptionEntry entries[] =
    {
      { "bus-address", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &bus_address,
        N_("Address of the D-Bus daemon to connect to and own a name on"),
        N_("ADDRESS") },
      { NULL, },
    };

  g_return_if_fail (HLP_IS_SERVICE (self));
  g_return_if_fail (argc > 0);
  g_return_if_fail (argv != NULL);

  HlpServicePrivate *priv = hlp_service_get_instance_private (self);
  HlpServiceClass *service_class = HLP_SERVICE_GET_CLASS (self);

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

  /* Set up signal handlers. */
  priv->sigint_id = g_unix_signal_add (SIGINT, signal_sigint_cb, self);
  priv->sigterm_id = g_unix_signal_add (SIGTERM, signal_sigterm_cb, self);

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
      return;
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

