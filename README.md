Mogwai
======

Mogwai allows systems to take advantage of reduced cost bandwidth at off-peak
times of day. It provides a monitoring daemon which checks bandwidth usage, a
scheduling daemon which prioritises downloads to minimise cost, and a tariff
library which describes different data plans.

All the library APIs are currently unstable and are likely to change wildly.

Architecture
------------

The architecture of Mogwai is designed to allow applications and services to
voluntarily schedule their downloads according to a system-wide download policy.
The policy can take things like the user’s internet tariff, the metered status
of the current connection, or the amount of battery power left, into account. In
future, it might also take into account the download history, for example by
checking the user is not near their limit of free download capacity for this
time period.

This policy is implemented by `mogwai-scheduled`, a system daemon which
applications can talk to over D-Bus (it takes the well-known name
`com.endlessm.MogwaiSchedule1`). If an application wants to do a download, it
creates a ‘schedule entry’ in Mogwai, containing details about the download
(such as its estimated size). The application then waits for `mogwai-scheduled`
to set the schedule entry’s `DownloadNow` property to true — at that point, the
application is free to start the download.

Note that the download happens inside the application’s address space:
`mogwai-scheduled` cannot actually do any network operations itself. This is
deliberate: providing support for every kind of network request and
authentication that an application might want would be too difficult and error
prone; and doing network operations from a system daemon would provide an easy
escalation vector for any system compromise.

`mogwai-scheduled` may set `DownloadNow` to false again in future, and at that
point the application must pause its download (if it hasn’t already finished).
This is typically because a higher-priority download has pre-empted it.

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
