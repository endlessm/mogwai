libhelper_api_version = '1'
libhelper_sources = [
  'service.c',
]
libhelper_headers = [
  'service.h',
]

libhelper_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('libsystemd'),
]

libhelper = static_library('helper-' + libhelper_api_version,
  libhelper_sources + libhelper_headers,
  dependencies: libhelper_deps,
  include_directories: root_inc,
  install: false,
)
libhelper_dep = declare_dependency(
  link_with: libhelper,
  include_directories: root_inc,
)

subdir('tests')
