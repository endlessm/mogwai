libmogwai_schedule_client_api_version = '1'
libmogwai_schedule_client_sources = [
  'schedule-entry.c',
  'scheduler.c',
]
libmogwai_schedule_client_headers = [
  'schedule-entry.h',
  'scheduler.h',
]

libmogwai_schedule_client_deps = [
  dependency('gio-2.0', version: '>= 2.46'),
  dependency('glib-2.0', version: '>= 2.54'),
  dependency('gobject-2.0', version: '>= 2.54'),
]

libmogwai_schedule_client = library('mogwai-schedule-client-' + libmogwai_schedule_client_api_version,
  libmogwai_schedule_client_sources + libmogwai_schedule_client_headers,
  dependencies: libmogwai_schedule_client_deps,
  include_directories: root_inc,
  install: true,
)
libmogwai_schedule_client_dep = declare_dependency(
  link_with: libmogwai_schedule_client,
  include_directories: root_inc,
)

subdir('tests')
