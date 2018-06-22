libmogwai_schedule_api_version = '1'
libmogwai_schedule_sources = [
  'clock.c',
  'clock-system.c',
  'connection-monitor.c',
  'connection-monitor-nm.c',
  'peer-manager.c',
  'peer-manager-dbus.c',
  'schedule-entry.c',
  'schedule-service.c',
  'scheduler.c',
  'service.c',
]
libmogwai_schedule_headers = [
  'clock.h',
  'clock-system.h',
  'connection-monitor.h',
  'connection-monitor-nm.h',
  'peer-manager.h',
  'peer-manager-dbus.h',
  'schedule-entry.h',
  'schedule-entry-interface.h',
  'schedule-service.h',
  'scheduler.h',
  'scheduler-interface.h',
  'service.h',
]

libmogwai_schedule_deps = [
  dependency('gio-2.0', version: '>= 2.46'),
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.54'),
  dependency('libnm', version: '>= 1.8.0'),
  libhelper_dep,
  libmogwai_tariff_dep,
]

libmogwai_schedule = static_library('mogwai-schedule-' + libmogwai_schedule_api_version,
  libmogwai_schedule_sources + libmogwai_schedule_headers,
  dependencies: libmogwai_schedule_deps,
  c_args: [
    '-DNM_VERSION_MIN_REQUIRED=(NM_ENCODE_VERSION (1, 8, 0))',
    '-DNM_VERSION_MAX_ALLOWED=(NM_ENCODE_VERSION (1, 10, 0))',
  ],
  include_directories: root_inc,
  install: false,
)
libmogwai_schedule_dep = declare_dependency(
  link_with: libmogwai_schedule,
  include_directories: root_inc,
)

subdir('tests')
