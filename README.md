Mogwai
======

Mogwai allows systems to take advantage of reduced cost bandwidth at off-peak
times of day. It provides a monitoring daemon which checks bandwidth usage, a
scheduling daemon which prioritises downloads to minimise cost, and a tariff
library which describes different data plans.

All the library APIs are currently unstable and are likely to change wildly.

Dependencies
------------

 * gio-2.0 ≥ 2.57.1
 * glib-2.0 ≥ 2.57.1
 * gobject-2.0 ≥ 2.57.1
 * libsoup-2.4 ≥ 2.42
 * systemd

Licensing
---------

The daemon components of Mogwai are licensed under the LGPL. The libraries are
also licensed under the LGPL. See COPYING for more details.

Bugs
----

Bug reports and patches should be reported by e-mail to one of the authors or
filed in GitLab:

https://gitlab.freedesktop.org/pwithnall/mogwai

Contact
-------

 * Philip Withnall <withnall@endlessm.com>
 * https://gitlab.freedesktop.org/pwithnall/mogwai
