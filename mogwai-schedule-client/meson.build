mogwai_scheduled_api_version = '0'
mogwai_schedule_client_sources = [
  'main.c',
]

mogwai_schedule_client_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('libsoup-2.4', version: '>= 2.42'),
  libmogwai_schedule_client_dep,
]

executable('mogwai-schedule-client-' + mogwai_scheduled_api_version,
  mogwai_schedule_client_sources,
  dependencies: mogwai_schedule_client_deps,
  install: true,
)

# Documentation
install_man('docs/mogwai-schedule-client.8')
