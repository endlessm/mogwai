libmogwai_schedule_api_version = '1'
libmogwai_schedule_sources = [
  'schedule-entry.c',
  'schedule-service.c',
  'scheduler.c',
  'service.c',
]
libmogwai_schedule_headers = [
  'schedule-entry.h',
  'schedule-entry-interface.h',
  'schedule-service.h',
  'scheduler.h',
  'scheduler-interface.h',
  'service.h',
]

libmogwai_schedule_deps = [
  dependency('gio-2.0', version: '>= 2.46'),
  dependency('glib-2.0', version: '>= 2.54'),
  dependency('gobject-2.0', version: '>= 2.54'),
  libhelper_dep,
]

libmogwai_schedule = static_library('mogwai-schedule-' + libmogwai_schedule_api_version,
  libmogwai_schedule_sources + libmogwai_schedule_headers,
  dependencies: libmogwai_schedule_deps,
  install: false,
)
libmogwai_schedule_dep = declare_dependency(link_with: libmogwai_schedule)

subdir('tests')
