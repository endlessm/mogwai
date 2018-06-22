libmogwai_schedule_client_api_version = '0'
libmogwai_schedule_client_api_name = 'mogwai-schedule-client-' + libmogwai_schedule_client_api_version
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
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.54'),
]

libmogwai_schedule_client = library(libmogwai_schedule_client_api_name,
  libmogwai_schedule_client_sources + libmogwai_schedule_client_headers,
  dependencies: libmogwai_schedule_client_deps,
  include_directories: root_inc,
  install: true,
  version: meson.project_version(),
  soversion: libmogwai_schedule_client_api_version,
)
libmogwai_schedule_client_dep = declare_dependency(
  link_with: libmogwai_schedule_client,
  include_directories: root_inc,
)

# Public library bits.
install_headers(libmogwai_schedule_client_headers,
  subdir: join_paths(libmogwai_schedule_client_api_name, 'libmogwai-schedule-client'),
)

pkgconfig.generate(
  libraries: [ libmogwai_schedule_client ],
  subdirs: libmogwai_schedule_client_api_name,
  version: meson.project_version(),
  name: 'libmogwai-schedule-client',
  filebase: libmogwai_schedule_client_api_name,
  description: 'Client library for download scheduling service.',
  # FIXME: This should be derived from libmogwai_schedule_client_deps.
  requires: [ 'gio-2.0', 'glib-2.0', 'gobject-2.0' ],
)

subdir('tests')
