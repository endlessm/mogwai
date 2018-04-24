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
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/schedule-entry.h>
#include <libmogwai-schedule/scheduler.h>


static void mws_schedule_entry_constructed  (GObject      *object);
static void mws_schedule_entry_dispose      (GObject      *object);

static void mws_schedule_entry_get_property (GObject      *object,
                                             guint         property_id,
                                             GValue       *value,
                                             GParamSpec   *pspec);
static void mws_schedule_entry_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec);

/**
 * MwsScheduleEntry:
 *
 * An entry in the scheduler representing a single download (either active or
 * inactive). This stores the scheduling parameters for the download as provided
 * by the app which is downloading it, but it does not store any of the
 * scheduler’s state.
 *
 * The ID for a #MwsSchedulerEntry is globally unique and never re-used. It’s
 * generated when the #MwsScheduleEntry is created.
 *
 * Since: 0.1.0
 */
struct _MwsScheduleEntry
{
  GObject parent;

  gchar *id;  /* (owned) (not nullable) */
  gchar *owner;  /* (owned) (not nullable) */

  gboolean resumable;
  guint32 priority;
};

typedef enum
{
  PROP_ID = 1,
  PROP_OWNER,
  PROP_RESUMABLE,
  PROP_PRIORITY,
} MwsScheduleEntryProperty;

G_DEFINE_TYPE (MwsScheduleEntry, mws_schedule_entry, G_TYPE_OBJECT)

static void
mws_schedule_entry_class_init (MwsScheduleEntryClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_PRIORITY + 1] = { NULL, };

  object_class->constructed = mws_schedule_entry_constructed;
  object_class->dispose = mws_schedule_entry_dispose;
  object_class->get_property = mws_schedule_entry_get_property;
  object_class->set_property = mws_schedule_entry_set_property;

  /**
   * MwsScheduleEntry:id:
   *
   * The unique, persistent ID for this schedule entry. It’s generated at
   * construction time, and never changes. It is suitable for use in a D-Bus
   * object path.
   *
   * Since: 0.1.0
   */
  props[PROP_ID] =
      g_param_spec_string ("id", "ID",
                           "Unique, persistent ID for this schedule entry.",
                           NULL,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleEntry:owner:
   *
   * The D-Bus unique name of the peer which created this schedule entry. This
   * must be set at construction time.
   *
   * Since: 0.1.0
   */
  props[PROP_OWNER] =
      g_param_spec_string ("owner", "Owner",
                           "D-Bus unique name of the peer which created this "
                           "schedule entry.",
                           NULL,
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleEntry:resumable:
   *
   * Whether pausing and resuming this download is supported by the owner after
   * it’s started. Some applications and servers can only restart downloads from
   * the beginning after pausing them.
   *
   * Since: 0.1.0
   */
  props[PROP_RESUMABLE] =
      g_param_spec_boolean ("resumable", "Resumable",
                            "Whether pausing and resuming this download is supported.",
                            FALSE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * MwsScheduleEntry:priority:
   *
   * The priority of this download relative to others belonging to the same
   * owner. Higher numbers mean the download is more important.
   *
   * Since: 0.1.0
   */
  props[PROP_PRIORITY] =
      g_param_spec_uint ("priority", "Priority",
                         "The priority of this download relative to others.",
                         0, G_MAXUINT32, 0,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static guint64 entry_id_counter = 0;
G_LOCK_DEFINE_STATIC (entry_id_counter);

static void
mws_schedule_entry_init (MwsScheduleEntry *self)
{
  /* Generate a unique ID for the entry. With a 64-bit counter, we can generate
   * a new #MwsScheduleEntry at 1GHz for over 500 years before we run out of
   * numbers. Just in case, abort if we hit the limit.
   * FIXME: Ideally we’d use 64-bit atomics here:
   * https://bugzilla.gnome.org/show_bug.cgi?id=754182 */
  G_LOCK (entry_id_counter);
  guint64 our_id = entry_id_counter;
  entry_id_counter++;
  G_UNLOCK (entry_id_counter);

  g_assert (our_id < G_MAXUINT64);
  self->id = g_strdup_printf ("%" G_GUINT64_FORMAT, our_id);
}

static void
mws_schedule_entry_constructed (GObject *object)
{
  MwsScheduleEntry *self = MWS_SCHEDULE_ENTRY (object);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_schedule_entry_parent_class)->constructed (object);

  /* Check all our construct-only properties are set. */
  g_assert (self->id != NULL && *self->id != '\0');
  g_assert (self->owner != NULL && g_dbus_is_unique_name (self->owner));
}

static void
mws_schedule_entry_dispose (GObject *object)
{
  MwsScheduleEntry *self = MWS_SCHEDULE_ENTRY (object);

  g_clear_pointer (&self->owner, g_free);
  g_clear_pointer (&self->id, g_free);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (mws_schedule_entry_parent_class)->dispose (object);
}

static void
mws_schedule_entry_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MwsScheduleEntry *self = MWS_SCHEDULE_ENTRY (object);

  switch ((MwsScheduleEntryProperty) property_id)
    {
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;
    case PROP_OWNER:
      g_value_set_string (value, self->owner);
      break;
    case PROP_RESUMABLE:
      g_value_set_boolean (value, self->resumable);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, self->priority);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_schedule_entry_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MwsScheduleEntry *self = MWS_SCHEDULE_ENTRY (object);

  switch ((MwsScheduleEntryProperty) property_id)
    {
    case PROP_ID:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_OWNER:
      /* Construct only. */
      g_assert (self->owner == NULL);
      g_assert (g_dbus_is_unique_name (g_value_get_string (value)));
      self->owner = g_value_dup_string (value);
      break;
    case PROP_RESUMABLE:
      mws_schedule_entry_set_resumable (self, g_value_get_boolean (value));
      break;
    case PROP_PRIORITY:
      mws_schedule_entry_set_priority (self, g_value_get_uint (value));
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * mws_schedule_entry_new:
 * @owner: the D-Bus unique name of the peer creating this entry
 *
 * Create a new #MwsScheduleEntry belonging to the bus peer @owner.
 *
 * Returns: (transfer full): a new #MwsScheduleEntry
 * Since: 0.1.0
 */
MwsScheduleEntry *
mws_schedule_entry_new (const gchar *owner)
{
  g_return_val_if_fail (g_dbus_is_unique_name (owner), NULL);

  return mws_schedule_entry_new_from_variant (owner, NULL, NULL);
}

/**
 * mws_schedule_entry_new_from_variant:
 * @owner: the D-Bus unique name of the peer creating this entry
 * @parameters: (nullable): #GVariant dictionary mapping parameter names to
 *    values, or %NULL to ignore
 * @error: return location for a #GError, or %NULL
 *
 * Create a new #MwsScheduleEntry belonging to the bus peer @owner, and with
 * its properties initially set to the values from @parameters. If any of the
 * @parameters are invalid (incorrect type or value), an error will be returned.
 * Any @parameters which are not understood by this version of the server will
 * be ignored without error.
 *
 * The following @parameters are currently supported:
 *
 *  * `resumable` (`b`): sets #MwsScheduleEntry:resumable
 *  * `priority` (`u`): sets #MwsScheduleEntry:priority
 *
 * Returns: (transfer full): a new #MwsScheduleEntry
 * Since: 0.1.0
 */
MwsScheduleEntry *
mws_schedule_entry_new_from_variant (const gchar  *owner,
                                     GVariant     *parameters,
                                     GError      **error)
{
  g_return_val_if_fail (g_dbus_is_unique_name (owner), NULL);
  g_return_val_if_fail (parameters == NULL ||
                        g_variant_is_of_type (parameters, G_VARIANT_TYPE_VARDICT), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_autoptr(GPtrArray) names = NULL;
  names = g_ptr_array_new_full (g_variant_n_children (parameters), NULL);
  g_autoptr(GArray) values = NULL;
  values = g_array_sized_new (FALSE, TRUE, sizeof (GValue),
                              g_variant_n_children (parameters));
  g_array_set_clear_func (values, (GDestroyNotify) g_value_unset);

  if (parameters != NULL)
    {
      GVariantIter iter;
      const gchar *key;
      g_autoptr(GVariant) variant = NULL;

      g_variant_iter_init (&iter, parameters);
      while (g_variant_iter_loop (&iter, "{&sv}", &key, &variant))
        {
          const struct
            {
              const gchar *name;
              const GVariantType *type;
            }
          supported_properties[] =
            {
              { "resumable", G_VARIANT_TYPE_BOOLEAN },
              { "priority", G_VARIANT_TYPE_UINT32 },
            };

          for (gsize i = 0; i < G_N_ELEMENTS (supported_properties); i++)
            {
              if (g_str_equal (supported_properties[i].name, key))
                {
                  if (!g_variant_is_of_type (variant, supported_properties[i].type))
                    {
                      g_set_error (error, MWS_SCHEDULER_ERROR,
                                   MWS_SCHEDULER_ERROR_INVALID_PARAMETERS,
                                   _("Invalid schedule entry parameters"));
                      return NULL;
                    }

                  GValue value = G_VALUE_INIT;
                  g_dbus_gvariant_to_gvalue (variant, &value);

                  g_ptr_array_add (names, (gpointer) key);
                  g_array_append_val (values, value);

                  break;
                }
            }
        }
    }

  /* Always add the owner. */
  g_ptr_array_add (names, (gpointer) "owner");
  GValue value = G_VALUE_INIT;
  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, owner);
  g_array_append_val (values, value);

  /* The cast of values->data to (const GValue *) triggers a -Wcast-align warning
   * on ARM without the cast through (void *). The array body is guaranteed to
   * be pointer aligned as the minimum array body size (from garray.c) is
   * 16 bytes. */
  g_assert (names->len == values->len);
  return MWS_SCHEDULE_ENTRY (g_object_new_with_properties (MWS_TYPE_SCHEDULE_ENTRY, names->len,
                                                           (const gchar **) names->pdata,
                                                           (const GValue *) (void *) values->data));
}

/**
 * mws_schedule_entry_get_id:
 * @self: a #MwsScheduleEntry
 *
 * Get the persistent identifier for this schedule entry. This is assigned when
 * at construction time, uniquely and persistently, and is never %NULL or the
 * empty string.
 *
 * Returns: identifier for the entry
 * Since: 0.1.0
 */
const gchar *
mws_schedule_entry_get_id (MwsScheduleEntry *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (self), NULL);

  g_assert (self->id != NULL && *self->id != '\0');
  return self->id;
}

/**
 * mws_schedule_entry_get_owner:
 * @self: a #MwsScheduleEntry
 *
 * Get the value of #MwsScheduleEntry:owner.
 *
 * Returns: the entry’s owner
 * Since: 0.1.0
 */
const gchar *
mws_schedule_entry_get_owner (MwsScheduleEntry *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (self), NULL);

  return self->owner;
}

/**
 * mws_schedule_entry_get_priority:
 * @self: a #MwsScheduleEntry
 *
 * Get the value of #MwsScheduleEntry:priority.
 *
 * Returns: the entry’s priority
 * Since: 0.1.0
 */
guint32
mws_schedule_entry_get_priority (MwsScheduleEntry *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (self), 0);

  return self->priority;
}

/**
 * mws_schedule_entry_set_priority:
 * @self: a #MwsScheduleEntry
 * @priority: the entry’s priority
 *
 * Set the value of #MwsScheduleEntry:priority.
 *
 * Since: 0.1.0
 */
void
mws_schedule_entry_set_priority (MwsScheduleEntry *self,
                                 guint32           priority)
{
  g_return_if_fail (MWS_IS_SCHEDULE_ENTRY (self));

  if (self->priority == priority)
    return;

  self->priority = priority;
  g_object_notify (G_OBJECT (self), "priority");
}

/**
 * mws_schedule_entry_get_resumable:
 * @self: a #MwsScheduleEntry
 *
 * Get the value of #MwsScheduleEntry:resumable.
 *
 * Returns: %TRUE if the download is resumable, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_schedule_entry_get_resumable (MwsScheduleEntry *self)
{
  g_return_val_if_fail (MWS_IS_SCHEDULE_ENTRY (self), FALSE);

  return self->resumable;
}

/**
 * mws_schedule_entry_set_resumable:
 * @self: a #MwsScheduleEntry
 * @resumable: %TRUE if the download is resumable, %FALSE otherwise
 *
 * Set the value of #MwsScheduleEntry:resumable.
 *
 * Since: 0.1.0
 */
void
mws_schedule_entry_set_resumable (MwsScheduleEntry *self,
                                  gboolean          resumable)
{
  g_return_if_fail (MWS_IS_SCHEDULE_ENTRY (self));

  resumable = !!resumable;
  if (self->resumable == resumable)
    return;

  self->resumable = resumable;
  g_object_notify (G_OBJECT (self), "resumable");
}
