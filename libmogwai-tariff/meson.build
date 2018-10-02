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

libmogwai_tariff_public_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.54'),
]

# FIXME: Would be good to use subdir here: https://github.com/mesonbuild/meson/issues/2969
libmogwai_tariff_include_subdir = join_paths(libmogwai_tariff_api_name, 'libmogwai-tariff')
libmogwai_tariff_enums = gnome.mkenums_simple('enums',
  sources: libmogwai_tariff_headers,
  install_header: true,
  install_dir: join_paths(includedir, libmogwai_tariff_include_subdir),
)

libmogwai_tariff = library(libmogwai_tariff_api_name,
  libmogwai_tariff_sources + libmogwai_tariff_headers,
  libmogwai_tariff_enums,
  dependencies: libmogwai_tariff_public_deps,
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
install_headers(libmogwai_tariff_headers,
  subdir: libmogwai_tariff_include_subdir,
)

pkgconfig.generate(
  libraries: [ libmogwai_tariff ],
  subdirs: libmogwai_tariff_api_name,
  version: meson.project_version(),
  name: 'libmogwai-tariff',
  filebase: libmogwai_tariff_api_name,
  description: 'Library describing network connection tariffs.',
  requires: libmogwai_tariff_public_deps,
)

if not cc.has_function('posix_memalign')
  error('posix_memalign() function not found')
endif

subdir('tests')
