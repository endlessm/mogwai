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
#include <libmogwai-tariff/tariff-builder.h>
#include <libmogwai-tariff/tariff-loader.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


/* FIXME: Add tests for this client. */

/* Error handling. */
typedef enum
{
  MWT_CLIENT_ERROR_INVALID_OPTIONS = 0,
  MWT_CLIENT_ERROR_LOOKUP_FAILED,
} MwtClientError;
#define MWT_CLIENT_N_ERRORS (MWT_CLIENT_ERROR_INVALID_OPTIONS + 1)

GQuark mwt_client_error_quark (void);
#define MWT_CLIENT_ERROR mwt_client_error_quark ()

G_DEFINE_QUARK (MwtClientError, mwt_client_error)

/* Exit statuses. */
typedef enum
{
  /* Success. */
  EXIT_OK = 0,
  /* Error parsing command line options. */
  EXIT_INVALID_OPTIONS = 1,
  /* Specified period couldn’t be found (for ‘lookup’ command). */
  EXIT_LOOKUP_FAILED = 2,
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

/* Output handling functions. */
static gchar *
dump_period (MwtPeriod *period,
             gboolean   use_colour)
{
  g_autoptr(GString) str = g_string_new ("");
  GDateTime *start = mwt_period_get_start (period);
  GDateTime *end = mwt_period_get_end (period);

  g_autofree gchar *start_str = g_date_time_format (start, "%Y-%m-%dT%H:%M:%S%:::z");
  g_autofree gchar *end_str = g_date_time_format (end, "%Y-%m-%dT%H:%M:%S%:::z");
  if (use_colour)
    g_string_append (str, "\033[1m");  /* bold */
  g_string_append_printf (str, _("Period %s – %s:"), start_str, end_str);
  if (use_colour)
    g_string_append (str, "\033[0m");  /* reset */
  g_string_append_c (str, '\n');

  g_autofree gchar *repeat_str = NULL;
  guint repeat_period = mwt_period_get_repeat_period (period);
  switch (mwt_period_get_repeat_type (period))
    {
    case MWT_PERIOD_REPEAT_NONE:
      repeat_str = g_strdup (_("Never repeats"));
      break;
    case MWT_PERIOD_REPEAT_HOUR:
      repeat_str = g_strdup_printf (g_dngettext (NULL,
                                                 "Repeats every %u hour",
                                                 "Repeats every %u hours",
                                                 repeat_period),
                                    repeat_period);
      break;
    case MWT_PERIOD_REPEAT_DAY:
      repeat_str = g_strdup_printf (g_dngettext (NULL,
                                                 "Repeats every %u day",
                                                 "Repeats every %u days",
                                                 repeat_period),
                                    repeat_period);
      break;
    case MWT_PERIOD_REPEAT_WEEK:
      repeat_str = g_strdup_printf (g_dngettext (NULL,
                                                 "Repeats every %u week",
                                                 "Repeats every %u weeks",
                                                 repeat_period),
                                    repeat_period);
      break;
    case MWT_PERIOD_REPEAT_MONTH:
      repeat_str = g_strdup_printf (g_dngettext (NULL,
                                                 "Repeats every %u month",
                                                 "Repeats every %u months",
                                                 repeat_period),
                                    repeat_period);
      break;
    case MWT_PERIOD_REPEAT_YEAR:
      repeat_str = g_strdup_printf (g_dngettext (NULL,
                                                 "Repeats every %u year",
                                                 "Repeats every %u years",
                                                 repeat_period),
                                    repeat_period);
      break;
    default:
      g_assert_not_reached ();
    }

  g_string_append_printf (str, " • %s\n", repeat_str);

  /* Properties. */
  guint64 capacity_limit = mwt_period_get_capacity_limit (period);
  g_autofree gchar *capacity_limit_str = NULL;
  g_autofree gchar *capacity_limit_value_str = NULL;
  if (capacity_limit == G_MAXUINT64)
    capacity_limit_value_str = g_strdup (_("unlimited"));
  else
    capacity_limit_value_str = g_format_size_full (capacity_limit,
                                                   G_FORMAT_SIZE_LONG_FORMAT);
  capacity_limit_str = g_strdup_printf (_("Capacity limit: %s"),
                                        capacity_limit_value_str);
  g_string_append_printf (str, " • %s\n", capacity_limit_str);

  return g_string_free (g_steal_pointer (&str), FALSE);
}

static gchar *
dump_tariff (MwtTariff *tariff,
             gboolean   use_colour)
{
  g_autoptr(GString) str = g_string_new ("");

  /* Add a title and underline it. */
  if (use_colour)
    g_string_append (str, "\033[1;4m");  /* bold, underlined */

  g_autofree gchar *title = g_strdup_printf (_("Tariff ‘%s’"), mwt_tariff_get_name (tariff));
  g_string_append (str, title);
  g_string_append_c (str, '\n');

  if (!use_colour)
    {
      for (gsize i = 0; i < g_utf8_strlen (title, -1); i++)
        g_string_append_c (str, '-');
      g_string_append_c (str, '\n');
    }

  if (use_colour)
    g_string_append (str, "\033[0m");  /* reset */

  g_string_append_c (str, '\n');

  GPtrArray *periods = mwt_tariff_get_periods (tariff);

  for (gsize i = 0; i < periods->len; i++)
    {
      MwtPeriod *period = g_ptr_array_index (periods, i);
      g_autofree gchar *out = dump_period (period, use_colour);
      g_string_append (str, out);
    }

  return g_string_free (g_steal_pointer (&str), FALSE);
}

/**
 * load_tariff_from_file:
 * @tariff_path: (type filename): tariff path, relative or absolute
 *
 * Returns: (transfer full): loaded tariff
 */
static MwtTariff *
load_tariff_from_file (const gchar  *tariff_path,
                       GError      **error)
{
  g_autofree gchar *tariff_path_utf8 = g_filename_display_name (tariff_path);

  /* Load the file. */
  g_autoptr(GMappedFile) mmap = NULL;

  mmap = g_mapped_file_new (tariff_path, FALSE  /* read-only */, error);
  if (mmap == NULL)
    {
      g_prefix_error (error, _("Error loading tariff file ‘%s’: "),
                      tariff_path_utf8);
      return NULL;
    }

  g_autoptr(GBytes) bytes = g_mapped_file_get_bytes (mmap);

  /* Load the tariff. */
  g_autoptr(MwtTariffLoader) loader = mwt_tariff_loader_new ();

  if (!mwt_tariff_loader_load_from_bytes (loader, bytes, error))
    {
      g_prefix_error (error, _("Error loading tariff file ‘%s’: "),
                      tariff_path_utf8);
      return NULL;
    }

  MwtTariff *tariff = mwt_tariff_loader_get_tariff (loader);
  g_assert (tariff != NULL);

  return g_object_ref (tariff);
}

static GDateTime *
date_time_from_string (const gchar  *str,
                       GError      **error)
{
  g_autoptr(GDateTime) date_time = NULL;
  date_time = g_date_time_new_from_iso8601 (str, NULL);

  if (date_time == NULL)
    {
      g_set_error (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_INVALID_OPTIONS,
                   _("Invalid ISO 8601 date/time ‘%s’."), str);
      return NULL;
    }

  return g_steal_pointer (&date_time);
}

static gboolean
repeat_type_from_string (const gchar          *str,
                         MwtPeriodRepeatType  *repeat_type_out,
                         GError              **error)
{
  g_return_val_if_fail (repeat_type_out != NULL, FALSE);

  const struct
    {
      MwtPeriodRepeatType repeat_type;
      const gchar *str;
    }
  repeat_types[] =
    {
      { MWT_PERIOD_REPEAT_NONE, "none" },
      { MWT_PERIOD_REPEAT_HOUR, "hour" },
      { MWT_PERIOD_REPEAT_DAY, "day" },
      { MWT_PERIOD_REPEAT_WEEK, "week" },
      { MWT_PERIOD_REPEAT_MONTH, "month" },
      { MWT_PERIOD_REPEAT_YEAR, "year" },
    };

  if (str == NULL)
    str = "";

  for (gsize i = 0; i < G_N_ELEMENTS (repeat_types); i++)
    {
      if (g_str_equal (str, repeat_types[i].str))
        {
          *repeat_type_out = repeat_types[i].repeat_type;
          return TRUE;
        }
    }

  g_set_error (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_INVALID_OPTIONS,
               _("Unknown repeat type ‘%s’."), str);
  return FALSE;
}

/* Accept an unsigned integer (to a guint64) or ‘unlimited’. */
static gboolean
capacity_limit_from_string (const gchar  *str,
                            guint64      *capacity_limit_out,
                            GError      **error)
{
  g_return_val_if_fail (capacity_limit_out != NULL, FALSE);

  if (str != NULL && g_str_equal (str, "unlimited"))
    {
      *capacity_limit_out = G_MAXUINT64;
      return TRUE;
    }

  return g_ascii_string_to_unsigned (str, 10, 0, G_MAXUINT64,
                                     capacity_limit_out, error);
}

/* Handle a command like
 *   mogwai-tariff lookup tariff.file 2018-10-02T10:15:00Z
 * by looking up which period applies at that time, and dumping its properties.
 */
static gboolean
handle_lookup (const gchar * const  *args,
               gboolean              use_colour,
               GError              **error)
{
  /* Parse arguments. */
  if (args == NULL || g_strv_length ((gchar **) args) != 2)
    {
      g_set_error_literal (error, MWT_CLIENT_ERROR,
                           MWT_CLIENT_ERROR_INVALID_OPTIONS,
                           _("A TARIFF and LOOKUP-TIME are required."));
      return FALSE;
    }

  /* Note @tariff_path is (type filename) not (type utf8). */
  const gchar *tariff_path = args[0];
  const gchar *lookup_time_str = args[1];

  g_autoptr(GDateTime) lookup_time = date_time_from_string (lookup_time_str, error);
  if (lookup_time == NULL)
    {
      g_prefix_error (error, _("Invalid LOOKUP-TIME: "));
      return FALSE;
    }

  /* Load the file. */
  g_autoptr(MwtTariff) tariff = load_tariff_from_file (tariff_path, error);
  if (tariff == NULL)
    return FALSE;

  MwtPeriod *period = mwt_tariff_lookup_period (tariff, lookup_time);

  if (period == NULL)
    {
      g_set_error (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_LOOKUP_FAILED,
                   _("No period matches the given date/time."));
      return FALSE;
    }
  else
    {
      g_autofree gchar *out = dump_period (period, use_colour);
      g_print ("%s", out);
    }

  return TRUE;
}

/* Handle a command like
 *   mogwai-tariff dump tariff.file
 * by dumping all the periods from that file, plus the file metadata.
 */
static gboolean
handle_dump (const gchar * const  *args,
             gboolean              use_colour,
             GError              **error)
{
  /* Parse arguments. */
  if (args == NULL || g_strv_length ((gchar **) args) != 1)
    {
      g_set_error_literal (error, MWT_CLIENT_ERROR,
                           MWT_CLIENT_ERROR_INVALID_OPTIONS,
                           _("A TARIFF is required."));
      return FALSE;
    }

  const gchar *tariff_path = args[0];

  g_autoptr(MwtTariff) tariff = load_tariff_from_file (tariff_path, error);
  if (tariff == NULL)
    return FALSE;

  g_autofree gchar *out = dump_tariff (tariff, use_colour);
  g_print ("%s", out);

  return TRUE;
}

/* Handle a command like
 *   mogwai-tariff build tariff.file name \
 *     [start end repeat-type repeat-period capacity-limit] \
 *     …
 * by building and saving the given tariff.
 */
static gboolean
handle_build (const gchar * const  *args,
              gboolean              use_colour,
              GError              **error)
{
  /* Parse arguments. */
  const guint n_args_per_period = 5;
  guint n_args = (args != NULL) ? g_strv_length ((gchar **) args) : 0;
  if (args == NULL || n_args < 2 + n_args_per_period ||
      ((n_args - 2) % n_args_per_period) != 0)
    {
      g_set_error_literal (error, MWT_CLIENT_ERROR,
                           MWT_CLIENT_ERROR_INVALID_OPTIONS,
                           _("A TARIFF and NAME and at least one PERIOD are required."));
      return FALSE;
    }

  const gchar *tariff_path = args[0];
  const gchar *tariff_name = args[1];

  g_autoptr(MwtTariffBuilder) builder = mwt_tariff_builder_new ();

  mwt_tariff_builder_set_name (builder, tariff_name);

  for (gsize i = 2; i < n_args; i += n_args_per_period)
    {
      /* Parse the arguments for this period. */
      const gchar *start_str = args[i];
      const gchar *end_str = args[i + 1];
      const gchar *repeat_type_str = args[i + 2];
      const gchar *repeat_period_str = args[i + 3];
      const gchar *capacity_limit_str = args[i + 4];

      g_autoptr(GDateTime) start = date_time_from_string (start_str, error);
      if (start == NULL)
        {
          g_prefix_error (error, _("Invalid START: "));
          return FALSE;
        }

      g_autoptr(GDateTime) end = date_time_from_string (end_str, error);
      if (end == NULL)
        {
          g_prefix_error (error, _("Invalid END: "));
          return FALSE;
        }

      MwtPeriodRepeatType repeat_type;
      if (!repeat_type_from_string (repeat_type_str, &repeat_type, error))
        {
          g_prefix_error (error, _("Invalid REPEAT-TYPE: "));
          return FALSE;
        }

      guint64 repeat_period64;
      if (!g_ascii_string_to_unsigned (repeat_period_str, 10, 0, G_MAXUINT,
                                       &repeat_period64, error))
        {
          g_prefix_error (error, _("Invalid REPEAT-PERIOD: "));
          return FALSE;
        }
      g_assert (repeat_period64 <= G_MAXUINT);
      guint repeat_period = repeat_period64;

      guint64 capacity_limit;
      if (!capacity_limit_from_string (capacity_limit_str, &capacity_limit, error))
        {
          g_prefix_error (error, _("Invalid CAPACITY-LIMIT: "));
          return FALSE;
        }

      if (!mwt_period_validate (start, end, repeat_type, repeat_period, error))
        {
          g_prefix_error (error, _("Error validating period %" G_GSIZE_FORMAT ": "),
                          (i - 2) / n_args_per_period);
          return FALSE;
        }

      g_autoptr(MwtPeriod) period = NULL;
      period = mwt_period_new (start, end, repeat_type, repeat_period,
                               "capacity-limit", capacity_limit,
                               NULL);
      mwt_tariff_builder_add_period (builder, period);
    }

  /* Save the tariff. */
  g_autoptr(GBytes) bytes = mwt_tariff_builder_get_tariff_as_bytes (builder);
  if (bytes == NULL)
    {
      /* FIXME: Would be good to change the #MwtTariffBuilder API to expose
       * the validation error here. */
      g_set_error (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_INVALID_OPTIONS,
                   _("Error building tariff: periods are invalid."));
      return FALSE;
    }

  if (!g_file_set_contents (tariff_path, g_bytes_get_data (bytes, NULL),
                            g_bytes_get_size (bytes), error))
    {
      g_autofree gchar *tariff_path_utf8 = g_filename_display_name (tariff_path);
      g_prefix_error (error, _("Error saving tariff file ‘%s’: "),
                      tariff_path_utf8);
      return FALSE;
    }

  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;

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

  /* Only valid for g_print() calls, not g_printerr(). */
  gboolean use_colour = g_log_writer_supports_color (fileno (stdout));

  /* Handle command line parameters. */
  g_auto (GStrv) args = NULL;

  const GOptionEntry entries[] =
    {
      { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY,
        &args, NULL, NULL },
      { NULL, },
    };

  g_autoptr(GOptionContext) context = NULL;
  context = g_option_context_new (_("COMMAND ARGS"));
  g_option_context_set_summary (context, _("Create and view network connection tariffs"));
  g_option_context_set_description (context,
      _("Commands:\n"
        "  build TARIFF NAME START END REPEAT-TYPE REPEAT-PERIOD CAPACITY-LIMIT […]\n"
        "    Build a new tariff called NAME and save it to the TARIFF file.\n"
        "    Add one or more periods using the given arguments.\n"
        "  dump TARIFF\n"
        "    Dump all periods from the given TARIFF file.\n"
        "  lookup TARIFF LOOKUP-TIME\n"
        "    Look up the period which covers LOOKUP-TIME in the given TARIFF file.\n"));
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 error->message);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  if (args == NULL || args[0] == NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 _("A COMMAND is required"));
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  /* Handle the different commands. */
  if (g_str_equal (args[0], "build"))
    handle_build ((const gchar * const *) args + 1, use_colour, &error);
  else if (g_str_equal (args[0], "dump"))
    handle_dump ((const gchar * const *) args + 1, use_colour, &error);
  else if (g_str_equal (args[0], "lookup"))
    handle_lookup ((const gchar * const *) args + 1, use_colour, &error);
  else
    g_set_error (&error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_INVALID_OPTIONS,
                 _("Unrecognised command ‘%s’"), args[1]);

  if (error != NULL)
    {
      int exit_status = EXIT_FAILED;

      if (g_error_matches (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_INVALID_OPTIONS))
        {
          g_prefix_error (&error, _("Option parsing failed: "));
          exit_status = EXIT_INVALID_OPTIONS;
        }
      else if (g_error_matches (error, MWT_CLIENT_ERROR, MWT_CLIENT_ERROR_LOOKUP_FAILED))
        {
          exit_status = EXIT_LOOKUP_FAILED;
        }

      g_printerr ("%s: %s\n", argv[0], error->message);

      return exit_status;
    }

  return EXIT_OK;
}
