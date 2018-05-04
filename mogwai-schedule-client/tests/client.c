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

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <locale.h>
#include <signal.h>


typedef struct
{
  gchar *tmpdir;  /* (owned) */
  GSubprocessLauncher *launcher;  /* (owned) */
} Fixture;

static void
setup (Fixture       *fixture,
       gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  fixture->launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                 G_SUBPROCESS_FLAGS_STDERR_PIPE);

  const gchar * const empty_array[1] = { NULL, };
  g_subprocess_launcher_set_environ (fixture->launcher, (gchar **) empty_array);

  fixture->tmpdir = g_dir_make_tmp ("mogwai-schedule-client-tests-basic-XXXXXX", &error);
  g_assert_no_error (error);
  g_subprocess_launcher_set_cwd (fixture->launcher, fixture->tmpdir);
}

static void
teardown (Fixture       *fixture,
          gconstpointer  test_data)
{
  g_assert_cmpint (g_rmdir (fixture->tmpdir), ==, 0);
  g_clear_pointer (&fixture->tmpdir, g_free);
  g_clear_object (&fixture->launcher);
}

static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

static void
timeout_cb (gpointer user_data)
{
  GSubprocess *process = G_SUBPROCESS (user_data);

  g_subprocess_send_signal (process, SIGINT);

  return G_SOURCE_REMOVE;
}

/* Block on @process completing, and assert that it was successful (zero exit
 * status, some output on stdout, no output on stderr).
 *
 * If @kill_timeout_seconds is greater than zero, SIGINT will be sent to the
 * subprocess after that many seconds. (This is intended for testing things like
 * monitor mode.) */
static void
assert_client_success (GSubprocess *process,
                       const gchar *tmpdir,
                       gboolean     quiet,
                       guint        kill_timeout_seconds)
{
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;

  /* Block on the subprocess completing. */
  g_autoptr(GAsyncResult) communicate_result = NULL;
  g_subprocess_communicate_utf8_async (process, NULL, NULL, async_result_cb,
                                       &communicate_result);

  g_autoptr(GSource) timeout_source = NULL;
  if (kill_timeout_seconds > 0)
    {
      timeout_source = g_timeout_source_new_seconds (kill_timeout_seconds);
      g_source_set_callback (timeout_source, timeout_cb, process, NULL);
      g_source_attach (timeout_source, NULL);
    }

  while (communicate_result == NULL)
    g_main_context_iteration (NULL, TRUE);

  if (timeout_source != NULL)
    g_source_destroy (timeout_source);

  g_subprocess_communicate_utf8_finish (process, communicate_result,
                                        &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  /* Check its output. */
  g_assert_true (g_subprocess_get_if_exited (process));
  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 0);
  if (quiet)
    g_assert_cmpstr (stdout_text, ==, "");
  else
    g_assert_cmpstr (stdout_text, !=, "");
  g_assert_cmpstr (stderr_text, ==, "");
}

/* Block on @process completing, and assert that it was successful (zero exit
 * status, some output on stdout, no output on stderr, `out` output file
 * created). */
static void
assert_client_download_success (GSubprocess *process,
                                const gchar *tmpdir,
                                gboolean     quiet)
{
  assert_client_success (process, tmpdir, quiet, 0);

  /* Check the file exists and is non-empty. */
  g_autofree gchar *out_path = g_build_filename (tmpdir, "out", NULL);

  GStatBuf stat_buf = { 0, };
  g_assert_cmpint (g_stat (out_path, &stat_buf), ==, 0);
  g_assert_true (stat_buf.st_mode & S_IFREG);
  g_assert_cmpuint (stat_buf.st_size, >, 0);

  /* Clean up. */
  g_unlink (out_path);
}

/* Test that something is successfully outputted for --help. */
static void
test_client_help (Fixture       *fixture,
                  gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client", "--help",
                                         NULL);
  g_assert_no_error (error);

  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 0);
  g_assert_cmpstr (stdout_text, !=, "");
  g_assert_cmpstr (stderr_text, ==, "");
}

/* Test error handling of various invalid command lines. */
static void
test_client_error_handling (Fixture       *fixture,
                            gconstpointer  test_data)
{
  const gchar * const vectors[][7] =
    {
      { "mogwai-schedule-client", NULL, },
      { "mogwai-schedule-client", "http://example.com/", NULL, },
      { "mogwai-schedule-client", "too", "many", "arguments", NULL, },
      { "mogwai-schedule-client", "-p", "not an int", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "-p", "-1", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "-p", "", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "download", NULL, },
      { "mogwai-schedule-client", "download", "http://example.com/", NULL, },
      { "mogwai-schedule-client", "download", "too", "many", "arguments", NULL, },
      { "mogwai-schedule-client", "download", "-p", "not an int", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "download", "-p", "-1", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "download", "-p", "", "http://example.com/", "out", NULL, },
      { "mogwai-schedule-client", "monitor", "too", "many", "arguments", NULL, },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT, i);

      g_autoptr(GSubprocess) process = NULL;
      process = g_subprocess_launcher_spawnv (fixture->launcher, vectors[i], &error);
      g_assert_no_error (error);

      g_autofree gchar *stdout_text = NULL;
      g_autofree gchar *stderr_text = NULL;

      g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
      g_assert_no_error (error);

      g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 1  /* invalid option */);
      g_assert_cmpstr (stdout_text, ==, "");
      g_assert_cmpstr (stderr_text, !=, "");
    }
}

/* Test that something is successfully outputted for `download --help`. */
static void
test_client_download_help (Fixture       *fixture,
                           gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client", "download", "--help",
                                         NULL);
  g_assert_no_error (error);

  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 0);
  g_assert_cmpstr (stdout_text, !=, "");
  g_assert_cmpstr (stderr_text, ==, "");
}

/* Test that a failed connection to D-Bus is correctly reported. */
static void
test_client_download_invalid_bus (Fixture       *fixture,
                                  gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client",
                                         "-a", "not a bus",
                                         "http://example.com/", "out",
                                         NULL);
  g_assert_no_error (error);

  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 2  /* couldn’t connect to bus */);
  g_assert_cmpstr (stdout_text, ==, "");
  g_assert_cmpstr (stderr_text, !=, "");
}

/* Test that something is downloaded with a simple test, and that status
 * information is printed along the way (unless we pass --quiet). */
static void
test_client_download_simple (Fixture       *fixture,
                             gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;
  gboolean quiet = GPOINTER_TO_INT (test_data);

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client",
                                         "http://example.com/", "out",
                                         quiet ? "--quiet" : NULL,
                                         NULL);
  g_assert_no_error (error);

  assert_client_download_success (process, fixture->tmpdir, quiet);
}

/* Test that something is downloaded when passing all arguments to the client. */
static void
test_client_download_all_arguments (Fixture       *fixture,
                                    gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client",
                                         "--priority", "5",
                                         "--resumable",
                                         "http://example.com/", "out",
                                         NULL);
  g_assert_no_error (error);

  assert_client_download_success (process, fixture->tmpdir, FALSE  /* not quiet */);
}

/* Test that something is successfully outputted for `monitor --help`. */
static void
test_client_monitor_help (Fixture       *fixture,
                          gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client", "monitor", "--help",
                                         NULL);
  g_assert_no_error (error);

  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 0);
  g_assert_cmpstr (stdout_text, !=, "");
  g_assert_cmpstr (stderr_text, ==, "");
}

/* Test that a failed connection to D-Bus is correctly reported. */
static void
test_client_monitor_invalid_bus (Fixture       *fixture,
                                 gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client",
                                         "monitor",
                                         "-a", "not a bus",
                                         NULL);
  g_assert_no_error (error);

  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, &stderr_text, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_subprocess_get_exit_status (process), ==, 2  /* couldn’t connect to bus */);
  g_assert_cmpstr (stdout_text, ==, "");
  g_assert_cmpstr (stderr_text, !=, "");
}

/* Test that signals are printed out when monitoring, and that status
 * information is printed along the way (unless we pass --quiet). */
static void
test_client_monitor_simple (Fixture       *fixture,
                            gconstpointer  test_data)
{
  g_autoptr(GError) error = NULL;
  gboolean quiet = GPOINTER_TO_INT (test_data);

  /* FIXME: We can’t guarantee that any signals will be emitted while we’re
   * monitoring. In order to make this test effective, we need a mock daemon.
   * (We need that in order to make any of these tests reliable anyway, since
   * they currently all depend on system connection metered state, so will fail
   * on any machine which doesn’t have an unlimited internet connection.) */
  g_autoptr(GSubprocess) process = NULL;
  process = g_subprocess_launcher_spawn (fixture->launcher, &error,
                                         "mogwai-schedule-client",
                                         "monitor",
                                         quiet ? "--quiet" : NULL,
                                         NULL);
  g_assert_no_error (error);

  assert_client_success (process, fixture->tmpdir, quiet, 2);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/client/help", Fixture, NULL, setup, test_client_help, teardown);
  g_test_add ("/client/error-handling", Fixture, NULL,
              setup, test_client_error_handling, teardown);
  g_test_add ("/client/download/help", Fixture, NULL, setup,
              test_client_download_help, teardown);
  g_test_add ("/client/download/invalid-bus", Fixture, NULL,
              setup, test_client_download_invalid_bus, teardown);
  g_test_add ("/client/download/simple", Fixture, GINT_TO_POINTER (FALSE)  /* !quiet */,
              setup, test_client_download_simple, teardown);
  g_test_add ("/client/download/simple/quiet", Fixture, GINT_TO_POINTER (TRUE)  /* quiet */,
              setup, test_client_download_simple, teardown);
  g_test_add ("/client/download/all-arguments", Fixture, NULL,
              setup, test_client_download_all_arguments, teardown);
  g_test_add ("/client/monitor/help", Fixture, NULL, setup,
              test_client_monitor_help, teardown);
  g_test_add ("/client/monitor/invalid-bus", Fixture, NULL,
              setup, test_client_monitor_invalid_bus, teardown);
  g_test_add ("/client/monitor/simple", Fixture, GINT_TO_POINTER (FALSE)  /* !quiet */,
              setup, test_client_monitor_simple, teardown);
  g_test_add ("/client/monitor/simple/quiet", Fixture, GINT_TO_POINTER (TRUE)  /* quiet */,
              setup, test_client_monitor_simple, teardown);

  return g_test_run ();
}
