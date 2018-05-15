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
#include <glib-object.h>
#include <gobject/gvaluecollector.h>
#include <libmogwai-schedule/tests/signal-logger.h>


/**
 * MwsSignalLogger:
 * @log: the logged signal emissions
 * @closures: the set of currently connected signal handler closures
 *
 * An object which allows signal emissions from zero or more #GObjects to be
 * logged easily, without needing to write specific callback functions for any
 * of them.
 *
 * Since: 0.1.0
 */
struct _MwsSignalLogger
{
  GPtrArray *log;  /* (element-type MwsSignalLoggerEmission) (owned) */
  GPtrArray *closures;  /* (element-type MwsLoggedClosure) (owned) */
};

/**
 * MwsLoggedClosure:
 * @closure: parent #GClosure
 * @logger: the #MwsSignalLogger this belongs to
 * @obj: pointer to the object instance this closure is connected to; no ref is
 *    held, and the object may be finalised before the closure, so this should
 *    only be used as an opaque pointer; add a #GWeakRef if the object needs to
 *    be accessed in future
 * @obj_type_name: a copy of `G_OBJECT_TYPE_NAME (obj)` for use when @obj may
 *    be invalid
 * @signal_name: name of the signal this closure is connected to, including
 *    detail (if applicable)
 * @signal_id: ID of the signal connection, or 0 if this closure has been
 *    disconnected
 *
 * A closure representing a connection from @logger to the given @signal_name
 * on @obj.
 *
 * The closure will be kept alive until the @logger is destroyed, though it will
 * be invalidated and disconnected earlier if @obj is finalised.
 *
 * Since: 0.1.0
 */
typedef struct
{
  GClosure closure;
  MwsSignalLogger *logger;  /* (not owned) */
  gpointer obj;  /* (not owned) */
  gchar *obj_type_name;  /* (owned) */
  gchar *signal_name;  /* (owned) */
  gulong signal_id;  /* 0 when disconnected */
} MwsLoggedClosure;

/**
 * MwsSignalLoggerEmission:
 * @closure: the closure this emission was captured by
 * @param_values: array of parameter values, not including the object instance
 * @n_param_values: number of elements in @param_values
 *
 * The details of a particular signal emission, including its parameter values.
 *
 * @param_values does not include the object instance.
 *
 * Since: 0.1.0
 */
struct _MwsSignalLoggerEmission
{
  MwsLoggedClosure *closure;  /* (owned) */
  GValue *param_values;  /* (array length=n_param_values) */
  gsize n_param_values;
};

/**
 * mws_signal_logger_emission_free:
 * @emission: (transfer full): a #MwsSignalLoggerEmission
 *
 * Free a #MwsSignalLoggerEmission.
 *
 * Since: 0.1.0
 */
void
mws_signal_logger_emission_free (MwsSignalLoggerEmission *emission)
{
  for (gsize i = 0; i < emission->n_param_values; i++)
    g_value_unset (&emission->param_values[i]);
  g_free (emission->param_values);

  g_closure_unref ((GClosure *) emission->closure);
  g_free (emission);
}

/**
 * mws_signal_logger_emission_get_params:
 * @self: a #MwsSignalLoggerEmission
 * @...: return locations for the signal parameters
 *
 * Get the parameters emitted in this signal emission. They are returned in the
 * return locations provided as varargs. These locations must have the right
 * type for the parameters of the signal which was emitted.
 *
 * To ignore a particular parameter, pass %NULL as the one (or more) return
 * locations for that parameter.
 *
 * Since: 0.1.0
 */
void
mws_signal_logger_emission_get_params (MwsSignalLoggerEmission *self,
                                       ...)
{
  va_list ap;

  va_start (ap, self);

  for (gsize i = 0; i < self->n_param_values; i++)
    {
      g_autofree gchar *error_message = NULL;
      G_VALUE_LCOPY (&self->param_values[i], ap, 0, &error_message);
      
      /* Error messages are not fatal, as they typically indicate that the user
       * has passed in %NULL rather than a valid return pointer. We can recover
       * from that. */
      if (error_message != NULL)
        g_debug ("Error copying GValue %" G_GSIZE_FORMAT " from emission of %s::%s from %p: %s",
                 i, self->closure->obj_type_name, self->closure->signal_name,
                 self->closure->obj, error_message);
    }

  va_end (ap);
}

static void
mws_logged_closure_marshal (GClosure     *closure,
                            GValue       *return_value,
                            guint         n_param_values,
                            const GValue *param_values,
                            gpointer      invocation_hint,
                            gpointer      marshal_data)
{
  MwsLoggedClosure *self = (MwsLoggedClosure *) closure;

  /* Log the @param_values. Ignore the @return_value, and the first of
   * @param_values (which is the object instance). */
  g_assert (n_param_values >= 1);

  g_autoptr(MwsSignalLoggerEmission) emission = g_new0 (MwsSignalLoggerEmission, 1);
  emission->closure = (MwsLoggedClosure *) g_closure_ref ((GClosure *) self);
  emission->n_param_values = n_param_values - 1;
  emission->param_values = g_new0 (GValue, emission->n_param_values);

  for (gsize i = 0; i < emission->n_param_values; i++)
    {
      g_value_init (&emission->param_values[i], G_VALUE_TYPE (&param_values[i + 1]));
      g_value_copy (&param_values[i + 1], &emission->param_values[i]);
    }

  g_ptr_array_add (self->logger->log, g_steal_pointer (&emission));
}

static void
mws_logged_closure_invalidate (gpointer  user_data,
                               GClosure *closure)
{
  MwsLoggedClosure *self = (MwsLoggedClosure *) closure;

  self->signal_id = 0;
}

static void
mws_logged_closure_finalize (gpointer  user_data,
                             GClosure *closure)
{
  MwsLoggedClosure *self = (MwsLoggedClosure *) closure;

  /* Deliberately don’t g_ptr_array_remove() the closure from the
   * self->logger->closures list, since finalize() can only be called when the
   * final reference to the closure is dropped, and self->logger->closures holds
   * a reference, so we must be being finalised from there (or that GPtrArray
   * has already been finalised). */

  g_free (self->obj_type_name);
  g_free (self->signal_name);

  g_assert (self->signal_id == 0);
}

/**
 * mws_logged_closure_new:
 * @logger: (transfer none): logger to connect the closure to
 * @obj: (not nullable) (transfer none): #GObject to connect the closure to
 * @signal_name: (not nullable): signal name to connect the closure to
 *
 * Create a new #MwsLoggedClosure for @logger, @obj and @signal_name. @obj must
 * be a valid object instance at this point (it may later be finalised before
 * the closure).
 *
 * This does not connect the closure to @signal_name on @obj. Use
 * mws_signal_logger_connect() for that.
 *
 * Returns: (transfer full): a new closure
 * Since: 0.1.0
 */
static GClosure *
mws_logged_closure_new (MwsSignalLogger *logger,
                        GObject         *obj,
                        const gchar     *signal_name)
{
  g_autoptr(GClosure) closure = g_closure_new_simple (sizeof (MwsLoggedClosure), NULL);

  MwsLoggedClosure *self = (MwsLoggedClosure *) closure;
  self->logger = logger;
  self->obj = obj;
  self->obj_type_name = g_strdup (G_OBJECT_TYPE_NAME (obj));
  self->signal_name = g_strdup (signal_name);
  self->signal_id = 0;

  g_closure_add_invalidate_notifier (closure, NULL, (GClosureNotify) mws_logged_closure_invalidate);
  g_closure_add_finalize_notifier (closure, NULL, (GClosureNotify) mws_logged_closure_finalize);
  g_closure_set_marshal (closure, mws_logged_closure_marshal);

  g_ptr_array_add (logger->closures, g_closure_ref (closure));

  return g_steal_pointer (&closure);
}

/**
 * mws_signal_logger_new:
 *
 * Create a new #MwsSignalLogger. Add signals to it to log using
 * mws_signal_logger_connect().
 *
 * Returns: (transfer full): a new #MwsSignalLogger
 * Since: 0.1.0
 */
MwsSignalLogger *
mws_signal_logger_new (void)
{
  g_autoptr(MwsSignalLogger) logger = g_new0 (MwsSignalLogger, 1);

  logger->log = g_ptr_array_new_with_free_func ((GDestroyNotify) mws_signal_logger_emission_free);
  logger->closures = g_ptr_array_new_with_free_func ((GDestroyNotify) g_closure_unref);

  return g_steal_pointer (&logger);
}

/**
 * mws_signal_logger_free:
 * @self: (transfer full): a #MwsSignalLogger
 *
 * Free a #MwsSignalLogger. This will disconnect all its closures from the
 * signals they are connected to.
 *
 * This function may be called when there are signal emissions left in the
 * logged stack, but typically you will want to call
 * mws_signal_logger_assert_no_emissions() first.
 *
 * Since: 0.1.0
 */
void
mws_signal_logger_free (MwsSignalLogger *self)
{
  g_return_if_fail (self != NULL);

  /* Disconnect all the closures, since we don’t care about logging any more. */
  for (gsize i = 0; i < self->closures->len; i++)
    {
      GClosure *closure = g_ptr_array_index (self->closures, i);

      g_closure_invalidate (closure);
    }

  g_ptr_array_unref (self->closures);
  g_ptr_array_unref (self->log);

  g_free (self);
}

/**
 * mws_signal_logger_connect:
 * @self: a #MwsSignalLogger
 * @obj: (type GObject): a #GObject to connect to
 * @signal_name: the signal on @obj to connect to
 *
 * A convenience wrapper around g_signal_connect() which connects the
 * #MwsSignalLogger to the given @signal_name on @obj so that emissions of it
 * will be logged.
 *
 * The closure will be disconnected (and the returned signal connection ID
 * invalidated) when:
 *
 *   * @obj is finalised
 *   * The closure is freed or removed
 *   * The signal logger is freed
 *
 * This does not keep a strong reference to @obj.
 *
 * Returns: signal connection ID, as returned from g_signal_connect()
 * Since: 0.1.0
 */
gulong
mws_signal_logger_connect (MwsSignalLogger *self,
                           gpointer         obj,
                           const gchar     *signal_name)
{
  g_return_val_if_fail (self != NULL, 0);
  g_return_val_if_fail (G_IS_OBJECT (obj), 0);
  g_return_val_if_fail (signal_name != NULL, 0);

  g_autoptr(GClosure) closure = mws_logged_closure_new (self, obj, signal_name);
  MwsLoggedClosure *c = (MwsLoggedClosure *) closure;
  c->signal_id = g_signal_connect_closure (obj, signal_name, g_closure_ref (closure), FALSE);
  return c->signal_id;
}

/**
 * mws_signal_logger_get_n_emissions:
 * @self: a #MwsSignalLogger
 *
 * Get the number of signal emissions which have been logged (and not popped)
 * since the logger was initialised.
 *
 * Returns: number of signal emissions
 * Since: 0.1.0
 */
gsize
mws_signal_logger_get_n_emissions (MwsSignalLogger *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->log->len;
}

/**
 * mws_signal_logged_pop_emission:
 * @self: a #MwsSignalLogger
 * @out_obj: (out) (transfer none) (optional) (not nullable): return location
 *    for the object instance which emitted the signal
 * @out_obj_type_name: (out) (transfer full) (optional) (not nullable): return
 *    location for the name of the type of @out_obj
 * @out_signal_name: (out) (transfer full) (optional) (not nullable): return
 *    location for the name of the emitted signal
 * @out_emission: (out) (transfer full) (optional) (not nullable): return
 *    location for the signal emission closure containing emission parameters
 *
 * Pop the oldest signal emission off the stack of logged emissions, and return
 * its object, signal name and parameters in the given return locations. All
 * return locations are optional: if they are all %NULL, this function just
 * performs a pop.
 *
 * If there are no signal emissions on the logged stack, %FALSE is returned.
 *
 * @out_obj does not return a reference to the object instance, as it may have
 * been finalised since the signal emission was logged. It should be treated as
 * an opaque pointer. The type name of the object is given as
 * @out_obj_type_name, which is guaranteed to be valid.
 *
 * Returns: %TRUE if an emission was popped and returned, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_signal_logger_pop_emission (MwsSignalLogger          *self,
                                gpointer                 *out_obj,
                                gchar                   **out_obj_type_name,
                                gchar                   **out_signal_name,
                                MwsSignalLoggerEmission **out_emission)
{
  g_return_val_if_fail (self != NULL, FALSE);

  if (self->log->len == 0)
    {
      if (out_obj != NULL)
        *out_obj = NULL;
      if (out_obj_type_name != NULL)
        *out_obj_type_name = NULL;
      if (out_signal_name != NULL)
        *out_signal_name = NULL;
      if (out_emission != NULL)
        *out_emission = NULL;

      return FALSE;
    }

  /* FIXME: Could do with g_ptr_array_steal() here.
   * https://bugzilla.gnome.org/show_bug.cgi?id=795376 */
  g_ptr_array_set_free_func (self->log, NULL);
  g_autoptr(MwsSignalLoggerEmission) emission = g_steal_pointer (&self->log->pdata[0]);
  g_ptr_array_remove_index (self->log, 0);
  g_ptr_array_set_free_func (self->log, (GDestroyNotify) mws_signal_logger_emission_free);

  if (out_obj != NULL)
    *out_obj = emission->closure->obj;
  if (out_obj_type_name != NULL)
    *out_obj_type_name = g_strdup (emission->closure->obj_type_name);
  if (out_signal_name != NULL)
    *out_signal_name = g_strdup (emission->closure->signal_name);
  if (out_emission != NULL)
    *out_emission = g_steal_pointer (&emission);

  return TRUE;
}

/**
 * mws_signal_logger_format_emission:
 * @obj: a #GObject instance which emitted a signal
 * @obj_type_name: a copy of `G_OBJECT_TYPE_NAME (obj)` for use when @obj may
 *    be invalid
 * @signal_name: name of the emitted signal
 * @emission: details of the signal emission
 *
 * Format a signal emission in a human readable form, typically for logging it
 * to some debug output.
 *
 * The returned string does not have a trailing newline character (`\n`).
 *
 * @obj may have been finalised, and is just treated as an opaque pointer.
 *
 * Returns: (transfer full): human readable string detailing the signal emission
 * Since: 0.1.0
 */
gchar *
mws_signal_logger_format_emission (gpointer                       obj,
                                   const gchar                   *obj_type_name,
                                   const gchar                   *signal_name,
                                   const MwsSignalLoggerEmission *emission)
{
  g_return_val_if_fail (obj != NULL, NULL);  /* deliberately not a G_IS_OBJECT() check */
  g_return_val_if_fail (signal_name != NULL, NULL);
  g_return_val_if_fail (emission != NULL, NULL);

  g_autoptr(GString) str = g_string_new ("");
  g_string_append_printf (str, "%s::%s from %p (",
                          obj_type_name, signal_name, obj);

  for (gsize i = 0; i < emission->n_param_values; i++)
    {
      if (i > 0)
        g_string_append (str, ", ");

      g_auto(GValue) str_value = G_VALUE_INIT;
      g_value_init (&str_value, G_TYPE_STRING);

      if (g_value_transform (&emission->param_values[i], &str_value))
        g_string_append (str, g_value_get_string (&str_value));
      else
        g_string_append_printf (str, "GValue of type %s",
                                G_VALUE_TYPE_NAME (&emission->param_values[i]));
    }

  if (emission->n_param_values == 0)
    g_string_append (str, "no arguments");
  g_string_append (str, ")");

  return g_string_free (g_steal_pointer (&str), FALSE);
}

/**
 * mws_signal_logger_format_emissions:
 * @self: a #MwsSignalLogger
 *
 * Format all the signal emissions on the logging stack in the #MwsSignalLogger,
 * in a human readable format, one per line. The returned string does not end
 * in a newline character (`\n`). Each signal emission is formatted using
 * mws_signal_logger_format_emission().
 *
 * Returns: (transfer full): human readable list of all the signal emissions
 *    currently in the logger, or an empty string if the logger is empty
 * Since: 0.1.0
 */
gchar *
mws_signal_logger_format_emissions (MwsSignalLogger *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  /* Work out the width of the counter we need to number the emissions. */
  guint width = 1;
  gsize n_emissions = self->log->len;
  while (n_emissions >= 10)
    {
      n_emissions /= 10;
      width++;
    }

  /* Format each emission and list them. */
  g_autoptr(GString) str = g_string_new ("");

  for (gsize i = 0; i < self->log->len; i++)
    {
      const MwsSignalLoggerEmission *emission = g_ptr_array_index (self->log, i);

      if (i > 0)
        g_string_append (str, "\n");

      g_autofree gchar *emission_str = mws_signal_logger_format_emission (emission->closure->obj,
                                                                          emission->closure->obj_type_name,
                                                                          emission->closure->signal_name,
                                                                          emission);
      g_string_append_printf (str, " %*" G_GSIZE_FORMAT ". %s", (int) width, i + 1, emission_str);
    }

  return g_string_free (g_steal_pointer (&str), FALSE);
}
