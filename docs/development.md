Development
===

When developing `mogwai-scheduled`, you can enable debug output from the daemon
using:
```
$ sudo systemctl edit --full mogwai-scheduled.service
# add `Environment=G_MESSAGES_DEBUG=all` to it
# You can now view debug output using:
$ sudo journalctl -b -u mogwai-scheduled.service
```

A `mogwai-schedule-client` utility is available as part of Mogwai, which
schedules a download and blocks until that download is complete. Typical usage
is:
```
$ mogwai-schedule-client http://test.com/ ./path/to/output
$ mogwai-schedule-client https://httpstat.us/?sleep=50000 ./path/to/output
```
