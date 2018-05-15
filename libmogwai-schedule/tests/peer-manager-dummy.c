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
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libmogwai-schedule/peer-manager.h>
#include <libmogwai-schedule/scheduler.h>
#include <libmogwai-schedule/tests/peer-manager-dummy.h>
#include <stdlib.h>


static void mws_peer_manager_dummy_peer_manager_init (MwsPeerManagerInterface *iface);

static void mws_peer_manager_dummy_get_property (GObject      *object,
                                                 guint         property_id,
                                                 GValue        *value,
                                                 GParamSpec   *pspec);
static void mws_peer_manager_dummy_set_property (GObject      *object,
                                                 guint         property_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec);
static void mws_peer_manager_dummy_finalize     (GObject      *object);

static void         mws_peer_manager_dummy_ensure_peer_credentials_async  (MwsPeerManager       *manager,
                                                                           const gchar          *sender,
                                                                           GCancellable         *cancellable,
                                                                           GAsyncReadyCallback   callback,
                                                                           gpointer              user_data);
static gchar       *mws_peer_manager_dummy_ensure_peer_credentials_finish (MwsPeerManager       *manager,
                                                                           GAsyncResult         *result,
                                                                           GError              **error);
static const gchar *mws_peer_manager_dummy_get_peer_credentials           (MwsPeerManager       *manager,
                                                                           const gchar          *sender);

/**
 * MwsPeerManagerDummy:
 *
 * An implementation of the #MwsPeerManager interface which returns dummy
 * results as provided using mws_peer_manager_dummy_set_peer_credentials() and
 * mws_peer_manager_dummy_remove_peer(). It can be set to always return failure
 * using mws_peer_manager_dummy_set_fail(). To be used for testing only.
 *
 * Since: 0.1.0
 */
struct _MwsPeerManagerDummy
{
  GObject parent;

  /* Whether to always fail, or always succeed. */
  gboolean fail;

  /* Mapping of D-Bus unique names to credentials to return. */
  GHashTable *peer_credentials;  /* (owned) (element-type utf8 utf8) */
};

typedef enum
{
  PROP_FAIL = 1,
} MwsPeerManagerDummyProperty;

G_DEFINE_TYPE_WITH_CODE (MwsPeerManagerDummy, mws_peer_manager_dummy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (MWS_TYPE_PEER_MANAGER,
                                                mws_peer_manager_dummy_peer_manager_init))
static void
mws_peer_manager_dummy_class_init (MwsPeerManagerDummyClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_FAIL + 1] = { NULL, };

  object_class->get_property = mws_peer_manager_dummy_get_property;
  object_class->set_property = mws_peer_manager_dummy_set_property;
  object_class->finalize = mws_peer_manager_dummy_finalize;

  /**
   * MwsPeerManagerDummy:fail:
   *
   * %TRUE to return %MWS_SCHEDULER_ERROR_IDENTIFYING_PEER when credentials are
   * next ensured; %FALSE to return valid details.
   *
   * Since: 0.1.0
   */
  props[PROP_FAIL] =
      g_param_spec_boolean ("fail", "Fail",
                            "Whether to return failure on calls to ensure peers.",
                            FALSE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
mws_peer_manager_dummy_peer_manager_init (MwsPeerManagerInterface *iface)
{
  iface->ensure_peer_credentials_async = mws_peer_manager_dummy_ensure_peer_credentials_async;
  iface->ensure_peer_credentials_finish = mws_peer_manager_dummy_ensure_peer_credentials_finish;
  iface->get_peer_credentials = mws_peer_manager_dummy_get_peer_credentials;
}

static void
mws_peer_manager_dummy_init (MwsPeerManagerDummy *self)
{
  self->peer_credentials = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_free);
}

static void
mws_peer_manager_dummy_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  MwsPeerManagerDummy *self = MWS_PEER_MANAGER_DUMMY (object);

  switch ((MwsPeerManagerDummyProperty) property_id)
    {
    case PROP_FAIL:
      g_value_set_boolean (value, self->fail);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_peer_manager_dummy_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  MwsPeerManagerDummy *self = MWS_PEER_MANAGER_DUMMY (object);

  switch ((MwsPeerManagerDummyProperty) property_id)
    {
    case PROP_FAIL:
      self->fail = g_value_get_boolean (value);
      g_object_notify (object, "fail");
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
mws_peer_manager_dummy_finalize (GObject *object)
{
  MwsPeerManagerDummy *self = MWS_PEER_MANAGER_DUMMY (object);

  g_clear_pointer (&self->peer_credentials, g_hash_table_unref);

  G_OBJECT_CLASS (mws_peer_manager_dummy_parent_class)->finalize (object);
}

static void
mws_peer_manager_dummy_ensure_peer_credentials_async (MwsPeerManager      *manager,
                                                      const gchar         *sender,
                                                      GCancellable        *cancellable,
                                                      GAsyncReadyCallback  callback,
                                                      gpointer             user_data)
{
  MwsPeerManagerDummy *self = MWS_PEER_MANAGER_DUMMY (manager);

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, mws_peer_manager_dummy_ensure_peer_credentials_async);

  const gchar *dummy_path = g_hash_table_lookup (self->peer_credentials, sender);

  if (self->fail || dummy_path == NULL)
    g_task_return_new_error (task, MWS_SCHEDULER_ERROR,
                             MWS_SCHEDULER_ERROR_IDENTIFYING_PEER,
                             "Dummy peer manager always returns this error");
  else
    g_task_return_pointer (task, g_strdup (dummy_path), g_free);
}

static gchar *
mws_peer_manager_dummy_ensure_peer_credentials_finish (MwsPeerManager  *manager,
                                                       GAsyncResult    *result,
                                                       GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, manager), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result, mws_peer_manager_dummy_ensure_peer_credentials_async), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static const gchar *
mws_peer_manager_dummy_get_peer_credentials (MwsPeerManager *manager,
                                             const gchar    *sender)
{
  MwsPeerManagerDummy *self = MWS_PEER_MANAGER_DUMMY (manager);

  return g_hash_table_lookup (self->peer_credentials, sender);
}

/**
 * mws_peer_manager_dummy_new:
 * @fail: %TRUE to always return %MWS_SCHEDULER_ERROR_IDENTIFYING_PEER; %FALSE
 *    to always return valid peer data
 *
 * Create a #MwsPeerManagerDummy object.
 *
 * Returns: (transfer full): a new #MwsPeerManagerDummy
 * Since: 0.1.0
 */
MwsPeerManagerDummy *
mws_peer_manager_dummy_new (gboolean fail)
{
  return g_object_new (MWS_TYPE_PEER_MANAGER_DUMMY,
                       "fail", fail,
                       NULL);
}

/**
 * mws_peer_manager_dummy_get_fail:
 * @self: a #MwsPeerManagerDummy
 *
 * Get the value of #MwsPeerManagerDummy:fail.
 *
 * Returns: %TRUE if the peer manager will always fail to get peer information;
 *    %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
mws_peer_manager_dummy_get_fail (MwsPeerManagerDummy *self)
{
  g_return_val_if_fail (MWS_IS_PEER_MANAGER_DUMMY (self), FALSE);

  return self->fail;
}

/**
 * mws_peer_manager_dummy_set_fail:
 * @self: a #MwsPeerManagerDummy
 * @fail: %TRUE to always fail to get peer information; %FALSE otherwise
 *
 * Set the value of #MwsPeerManagerDummy:fail to @fail.
 *
 * Since: 0.1.0
 */
void
mws_peer_manager_dummy_set_fail (MwsPeerManagerDummy *self,
                                 gboolean             fail)
{
  g_return_if_fail (MWS_IS_PEER_MANAGER_DUMMY (self));

  if (fail != self->fail)
    {
      self->fail = fail;
      g_object_notify (G_OBJECT (self), "fail");
    }
}

/**
 * mws_peer_manager_dummy_set_peer_credentials:
 * @self: a #MwsPeerManagerDummy
 * @sender: D-Bus unique name for the peer
 * @path: (nullable): absolute path to return as the credentials for @sender,
 *    or %NULL to remove the peer
 *
 * Set the mock credentials which will be returned for @sender when its
 * credentials are queried. If @path is %NULL, any existing mock credentials
 * will be removed and, if they were present, a #MwsPeerManager::peer-vanished
 * signal will be emitted.
 *
 * Since: 0.1.0
 */
void
mws_peer_manager_dummy_set_peer_credentials (MwsPeerManagerDummy *self,
                                             const gchar         *sender,
                                             const gchar         *path)
{
  g_return_if_fail (MWS_IS_PEER_MANAGER_DUMMY (self));
  g_return_if_fail (g_dbus_is_unique_name (sender));
  g_return_if_fail (path == NULL || *path == '/');

  if (path != NULL)
    {
      g_hash_table_replace (self->peer_credentials, g_strdup (sender), g_strdup (path));
    }
  else
    {
      if (g_hash_table_remove (self->peer_credentials, sender))
        g_signal_emit_by_name (self, "peer-vanished", sender);
    }
}

/**
 * mws_peer_manager_dummy_remove_peer:
 * @self: a #MwsPeerManagerDummy
 * @name: D-Bus unique name for the peer to remove
 *
 * Remove any existing mock credentials for @name. If credentials were removed,
 * this emits the #MwsPeerManager::peer-vanished signal.
 *
 * This function is equivalent to calling
 * mws_peer_manager_dummy_set_peer_credentials() with %NULL credentials.
 *
 * Since: 0.1.0
 */
void
mws_peer_manager_dummy_remove_peer (MwsPeerManagerDummy *self,
                                    const gchar         *name)
{
  g_return_if_fail (MWS_IS_PEER_MANAGER_DUMMY (self));
  g_return_if_fail (g_dbus_is_unique_name (name));

  mws_peer_manager_dummy_set_peer_credentials (self, name, NULL);
}
