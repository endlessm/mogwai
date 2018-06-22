project('mogwai','c',
  version: '0.1.0',
  meson_version: '>= 0.43.0',
  license: 'LGPLv2.1+',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

# Dependency on GLib. We depend on 2.57.1, which isn’t packaged yet. When it
# is packaged in Debian Unstable, we can drop this subproject dependency (FIXME).
# FIXME: Endless’ GLib 2.54.2 has the necessary backported patches, so we drop
# the subproject build to avoid ever accidentally using it.
libglib_dep = dependency('glib-2.0', version: '>= 2.54.2')

gnome = import('gnome')
pkgconfig = import('pkgconfig')

prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localedir = join_paths(prefix, get_option('localedir'))
includedir = join_paths(prefix, get_option('includedir'))

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('LOCALEDIR', localedir)
configure_file(
  output: 'config.h',
  configuration: config_h,
)
root_inc = include_directories('.')

# Enable warning flags
test_c_args = [
  '-fno-strict-aliasing',
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Wall',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wno-declaration-after-statement',
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wincompatible-pointer-types',
  '-Wincompatible-pointer-types-discards-qualifiers',
  '-Winit-self',
  '-Wint-conversion',
  '-Wlogical-op',
  '-Wmisleading-indentation',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-error=cpp',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wnull-dereference',
  '-Wold-style-definition',
  '-Woverflow',
  '-Woverride-init',
  '-Wparentheses',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing=2',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wunused-result',
  '-Wunused-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(test_c_args), language: 'c')

enable_installed_tests = get_option('installed_tests')
test_template = files('template.test.in')
test_env = [
  'G_DEBUG=gc-friendly,fatal-warnings',
  'MALLOC_CHECK_=2',
  'LC_ALL=C.UTF-8',
]

subdir('libhelper')
subdir('libmogwai-tariff')
subdir('libmogwai-schedule')
subdir('libmogwai-schedule-client')
subdir('mogwai-scheduled')
subdir('mogwai-schedule-client')
subdir('mogwai-tariff')
