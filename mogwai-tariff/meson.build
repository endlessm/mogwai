mogwai_tariff_sources = [
  'main.c',
]

mogwai_tariff_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.44'),
  libmogwai_tariff_dep,
]

executable('mogwai-tariff-' + libmogwai_tariff_api_version,
  mogwai_tariff_sources,
  dependencies: mogwai_tariff_deps,
  include_directories: root_inc,
  install: true,
)

# Documentation
install_man('docs/mogwai-tariff.8')

subdir('tests')
