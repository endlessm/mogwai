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

#include <gio/gio.h>
#include <glib.h>
#include <libmnl/libmnl.h>
#include <libnetfilter_acct/libnetfilter_acct.h>
#include <locale.h>
#include <stdlib.h>

typedef struct mnl_socket MgmNetlinkSocket;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MgmNetlinkSocket, mnl_socket_close)

static gboolean
add_nfacct (MgmNetlinkSocket  *mnl,
            const gchar       *name,
            GError           **error)
{
  char buf[MNL_SOCKET_BUFFER_SIZE];
  struct nlmsghdr *nlh;
  uint32_t seq;
  struct nfacct *acct;
  int ret;

  acct = nfacct_alloc ();
  if (acct == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "OOM");
      return FALSE;
    }

  nfacct_attr_set (acct, NFACCT_ATTR_NAME, name);

  seq = time (NULL);
  nlh = nfacct_nlmsg_build_hdr (buf, NFNL_MSG_ACCT_NEW,
                                NLM_F_CREATE | NLM_F_ACK, seq);
  nfacct_nlmsg_build_payload (nlh, acct);

  nfacct_free (acct);

  if (mnl_socket_sendto (mnl, nlh, nlh->nlmsg_len) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to send message");
      return FALSE;
    }

  uint32_t port_id = mnl_socket_get_portid (mnl);
  ret = mnl_socket_recvfrom (mnl, buf, sizeof (buf));
  while (ret > 0)
    {
      ret = mnl_cb_run (buf, ret, seq, port_id, NULL, NULL);
      if (ret <= 0)
        break;
      ret = mnl_socket_recvfrom (mnl, buf, sizeof (buf));
    }
  if (ret == -1)
    {
      /* FIXME: Ignore failure for the moment, as this also happens when creating
       * an already-existing nfacct object. Need to work out how to differentiate
       * failure modes.
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to receive reply");
      return FALSE;
       */
    }

  return TRUE;
}

static MgmNetlinkSocket *
netlink_open (GError **error)
{
  g_autoptr(MgmNetlinkSocket) mnl = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  mnl = mnl_socket_open (NETLINK_NETFILTER);
  if (mnl == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "OOM");
      return NULL;
    }

  if (mnl_socket_bind (mnl, 0, MNL_SOCKET_AUTOPID) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to bind socket");
      return NULL;
    }

  return g_steal_pointer (&mnl);
}

static gboolean
add_cgroup (MgmNetlinkSocket  *mnl,
            const gchar       *cgroup_path,
            GError           **error)
{
  g_autofree gchar *tx_nfacct_name = g_strdup_printf ("mogwai:%s:tx", cgroup_path);
  g_autofree gchar *rx_nfacct_name = g_strdup_printf ("mogwai:%s:rx", cgroup_path);

  /* Add some nfacct objects. */
  if (!add_nfacct (mnl, tx_nfacct_name, error))
    {
      g_prefix_error (error, "Error creating nfacct ‘%s’: ", tx_nfacct_name);
      return FALSE;
    }
  if (!add_nfacct (mnl, rx_nfacct_name, error))
    {
      g_prefix_error (error, "Error creating nfacct ‘%s’: ", rx_nfacct_name);
      return FALSE;
    }

  /* Add some iptables rules.
   * FIXME: This is horrific. And doesn’t handle errors. */
  /* FIXME: Looks like nfacct truncates object names to 31 characters, and
   * longer names which differ in their subsequent characters become aliased.
   * At the moment, this becomes: `mogwai:user.slice/user-1000.sli`. So we need
   * to do some kind of hashing or internal name map. Should be deterministic so
   * we can recover from mogwai-monitord crashing. */
  g_autofree gchar *iptables_tx_command =
      g_strdup_printf ("iptables -I INPUT -m cgroup --path %s -m nfacct --nfacct-name %s",
                       cgroup_path, tx_nfacct_name);
  g_autofree gchar *iptables_rx_command =
      g_strdup_printf ("iptables -I OUTPUT -m cgroup --path %s -m nfacct --nfacct-name %s",
                       cgroup_path, rx_nfacct_name);

  system (iptables_tx_command);
  system (iptables_rx_command);
  g_critical ("FIXME: Using system(). This code must not be used in production.");

  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;

  setlocale (LC_ALL, "");

  /* For the moment, we must be run as root. */
  if (geteuid () != 0)
    {
      g_printerr ("%s: Must be run as root\n", argv[0]);
      return 1;
    }

  g_autoptr(MgmNetlinkSocket) mnl = netlink_open (&error);

  g_autoptr(GDBusConnection) session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (session_bus == NULL)
    {
      g_printerr ("%s: Error connection to session bus: %s\n", argv[0], error->message);
      return 1;
    }

  /* TODO: Unsubscribe */
  guint signal_id = g_dbus_connection_signal_subscribe (session_bus,
                                                        "org.freedesktop.systemd1",
                                                        "org.freedesktop.systemd1.Manager",
                                                        "UnitNew",
                                                        "/org/freedesktop/systemd1",
                                                        NULL,
                                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                                        unit_new_cb,
                                                        NULL,
                                                        NULL);

  return 0;
}

static void
unit_new_cb (GDBusConnection *connection,
             const gchar     *sender_name,
             const gchar     *object_path,
             const gchar     *interface_name,
             const gchar     *signal_name,
             GVariant        *parameters,
             gpointer         user_data)
{
  g_autoptr(GError) error = NULL;
  const gchar *unit_name, *object_path;

  g_variant_get (parameters, "(&s&o)", &unit_name, &object_path);

  g_autofree gchar *cgroup_path = get_cgroup_path_for_unit (connection, sender_name, object_path);

  if (!add_cgroup (mnl, cgroup_path, &error))
    {
      g_printerr ("%s: Error handling cgroup ‘%s’: %s\n", argv[0], cgroup_path, error->message);
      return;
    }
}

/* For example, user.slice/user-1000.slice/user@1000.service/flatpak-org.gnome.Recipes-17558.scope. */
static gchar *
get_cgroup_path_for_unit (GDBusConnection  *connection,
                          const gchar      *sender_name,
                          const gchar      *object_path,
                          GCancellable     *cancellable,
                          GError          **error)
{
  g_autoptr(GDBusProxy) unit_proxy =
      g_dbus_proxy_new_sync (connection,
                             G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                             NULL, sender_name, object_path,
                             "org.freedesktop.DBus.Properties",
                             cancellable, error);
  if (unit_proxy == NULL)
    return NULL;

  g_autoptr(GVariant) result =
      g_dbus_proxy_call_sync (unit_proxy,
                              "Get",
                              g_variant_new ("(ss)",
                                             "org.freedesktop.systemd1.Scope",
                                             "ControlGroup"),
                              G_DBUS_CALL_FLAGS_NONE, -1,
                              cancellable, error);
  if (result == NULL)
    return NULL;

  return g_variant_dup_string (result, NULL);
}
