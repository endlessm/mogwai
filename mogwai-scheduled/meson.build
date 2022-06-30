mogwai_scheduled_api_version = '1'
mogwai_scheduled_sources = [
  'main.c',
]

mogwai_scheduled_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  libglib_dep,
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('gsystemservice-0', version: '>= 0.1.0'),
  libmogwai_schedule_dep,
]

mogwai_scheduled = executable('mogwai-scheduled' + mogwai_scheduled_api_version,
  mogwai_scheduled_sources,
  dependencies: mogwai_scheduled_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd, polkit and D-Bus files
config = configuration_data()
config.set('DAEMON_USER', get_option('scheduler_daemon_user'))
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'com.endlessm.MogwaiSchedule1.conf.in',
  output: 'com.endlessm.MogwaiSchedule1.conf',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'system.d'),
  configuration: config,
)
configure_file(
  input: 'com.endlessm.MogwaiSchedule1.service.in',
  output: 'com.endlessm.MogwaiSchedule1.service',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'system-services'),
  configuration: config,
)
configure_file(
  input: 'mogwai-scheduled.conf.in',
  output: 'mogwai-scheduled.conf',
  install_dir: dependency('systemd').get_pkgconfig_variable('sysusersdir'),
  configuration: config,
)
configure_file(
  input: 'mogwai-scheduled.service.in',
  output: 'mogwai-scheduled.service',
  install_dir: dependency('systemd').get_pkgconfig_variable('systemdsystemunitdir'),
  configuration: config,
)
configure_file(
  input: 'com.endlessm.MogwaiSchedule1.rules.in',
  output: 'com.endlessm.MogwaiSchedule1.rules',
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'rules.d'),
  configuration: config,
)

# Documentation
install_man('docs/mogwai-scheduled.8')

subdir('tests')
