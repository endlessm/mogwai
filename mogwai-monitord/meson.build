mogwai_monitord_sources = [
  'main.c',
]

mogwai_monitord_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('libmnl'),
  dependency('libnetfilter_acct'),
  dependency('libnftnl'),
]

executable('mogwai-monitord', mogwai_monitord_sources,
  dependencies: mogwai_monitord_deps,
  install: true,
)
