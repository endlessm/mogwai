libmogwai_tariff_api_version = '0'
libmogwai_tariff_api_name = 'mogwai-tariff-' + libmogwai_tariff_api_version
libmogwai_tariff_sources = [
  'period.c',
  'tariff-builder.c',
  'tariff-loader.c',
  'tariff.c',
]
libmogwai_tariff_headers = [
  'period.h',
  'tariff-builder.h',
  'tariff-loader.h',
  'tariff.h',
]

libmogwai_tariff_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.50'),
  dependency('gobject-2.0', version: '>= 2.54'),
]

libmogwai_tariff_enums = gnome.mkenums_simple('enums',
  sources: libmogwai_tariff_headers,
  install: true,
)

libmogwai_tariff = library(libmogwai_tariff_api_name,
  libmogwai_tariff_sources + libmogwai_tariff_headers,
  libmogwai_tariff_enums,
  dependencies: libmogwai_tariff_deps,
  include_directories: root_inc,
  install: true,
  version: meson.project_version(),
  soversion: libmogwai_tariff_api_version,
)
libmogwai_tariff_dep = declare_dependency(
  link_with: libmogwai_tariff,
  include_directories: root_inc,
)

# Public library bits.
# FIXME: Install enums.h too. Listing libmogwai_tariff_enums[1] here doesn’t work.
install_headers(libmogwai_tariff_headers,
  subdir: join_paths(libmogwai_tariff_api_name, 'libmogwai-tariff'),
)

pkgconfig.generate(
  libraries: [ libmogwai_tariff ],
  subdirs: libmogwai_tariff_api_name,
  version: meson.project_version(),
  name: 'libmogwai-tariff',
  filebase: libmogwai_tariff_api_name,
  description: 'Library describing network connection tariffs.',
  # FIXME: This should be derived from libmogwai_tariff_deps.
  requires: [ 'gio-2.0', 'glib-2.0', 'gobject-2.0' ],
)
