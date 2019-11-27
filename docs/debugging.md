Debugging
===

Mogwai will automatically download updates on connections which are considered
‘not metered’. A metered connection is one where each gigabyte of download costs
the user money (i.e. the marginal cost of download capacity is non-zero).

Mogwai queries NetworkManager for the metered status of each connection.
Specifically, it pessimistically combines the metered status of each `NMDevice`
and `NMSettingConnection` associated that that `NMConnection` object. Typically,
NetworkManager provides no metered status for an `NMDevice` unless that device
is a paired Bluetooth modem (which are considered to be metered), or it exposes
a DHCP `ANDROID_METERED` vendor-encapsulated option. The `NMSettingConnection`
metered status is entirely set by the user through the connection preferences
UI, but is remembered over time.

Incorrect detection of metered connections
---

If Mogwai does not think your connection is metered, but you do, it’s because
NetworkManager has not detected the connection as metered, probably because it
has a weird topography.

Please file an issue with the output of:
```
$ nmcli connection
$ nmcli connection show $connection_uuid
$ # unplug/disconnect the network device/dongle
$ nmcli general logging level DEBUG
$ # re-plug-in the network device/dongle
$ # wait for its network connection to be activated (note: metered status is only calculated for connections which are not disconnected or failed)
$ sudo journalctl -b -u NetworkManager.service
```

Debugging scheduler problems
---

You can use the `mogwai-schedule-client` program to manually schedule downloads,
passing it arguments to affect its scheduling parameters. You will need to
install the `mogwai-schedule-tools` package (on Debian systems) to get the
binary though.

To debug the scheduling daemon, run it with debug messages enabled:
```
$ sudo systemctl edit --full mogwai-scheduled.service
$ # add Environment=G_MESSAGES_DEBUG=all
$ sudo systemctl restart mogwai-scheduled.service
$ sudo journalctl -b -u mogwai-scheduled.service
```

Changing the settings on a connection
---

When debugging, it can sometimes be useful to forcibly change the metered data
settings on a NetworkManager connection. This can be done by editing the
connection’s configuration file in `/etc/NetworkManager/system-connections/`:
add `connection.allow-downloads=0` or `connection.allow-downloads-when-metered=0`
to it. Then run:
```
sudo nmcli connection down "${connection_name}"
sudo nmcli connection up "${connection_name}"
```

You can check that Mogwai has detected the change by running
`mogwai-schedule-client-1 monitor` as you do this; it will print a line saying
that `allow-downloads` has changed.
