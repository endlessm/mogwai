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
#include <gio/gio.h>
#include <libhelper/service.h>
#include <locale.h>


/* Test subclass of #HlpService. */
#define TEST_TYPE_SERVICE test_service_get_type ()
G_DECLARE_FINAL_TYPE (TestService, test_service, TEST, SERVICE, HlpService)

struct _TestService
{
  HlpService parent;
};

G_DEFINE_TYPE (TestService, test_service, HLP_TYPE_SERVICE)

static void
test_service_class_init (TestServiceClass *klass)
{
}

static void
test_service_init (TestService *self)
{
}

/* Test constructing an #HlpService object. Print its address to stop the
 * compiler complaining about an unused variable. */
static void
test_service_construction (void)
{
  g_autoptr(HlpService) service = NULL;
  service = g_object_new (TEST_TYPE_SERVICE,
                          "service-id", "com.endlessm.libhelper.tests.Service",
                          "translation-domain", "domain",
                          "parameter-string", "Blah",
                          NULL);
  g_test_message ("Service constructed as %p", service);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/service/construction", test_service_construction);

  return g_test_run ();
}
