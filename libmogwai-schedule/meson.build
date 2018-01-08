libmogwai_schedule_api_version = '1'
libmogwai_schedule_sources = [
  'service.c',
]
libmogwai_schedule_headers = [
  'service.h',
]

libmogwai_schedule_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.54'),
  libhelper_dep,
]

libmogwai_schedule = static_library('mogwai-schedule-' + libmogwai_schedule_api_version,
  libmogwai_schedule_sources + libmogwai_schedule_headers,
  dependencies: libmogwai_schedule_deps,
  include_directories: root_inc,
  install: false,
)
libmogwai_schedule_dep = declare_dependency(link_with: libmogwai_schedule)

subdir('tests')
