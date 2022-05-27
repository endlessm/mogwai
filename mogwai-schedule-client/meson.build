mogwai_schedule_client_sources = [
  'main.c',
]

mogwai_schedule_client_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('libsoup-3.0'),
  libmogwai_schedule_client_dep,
]

executable('mogwai-schedule-client-' + mogwai_scheduled_api_version,
  mogwai_schedule_client_sources,
  dependencies: mogwai_schedule_client_deps,
  include_directories: root_inc,
  install: true,
)

# Documentation
install_man('docs/mogwai-schedule-client.8')

subdir('tests')
