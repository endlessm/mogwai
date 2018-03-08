py3_mod = import('python3')
py3 = py3_mod.find_python()

envs = [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
  'G_DEBUG=gc-friendly',
  'MALLOC_CHECK_=2',
]

test_programs = [
  'mogwai-tariff.py',
]

installed_tests_metadir = join_paths(datadir, 'installed-tests',
                                     'mogwai-tariff-' + libmogwai_tariff_api_version)
installed_tests_execdir = join_paths(libexecdir, 'installed-tests',
                                     'mogwai-tariff-' + libmogwai_tariff_api_version)

foreach program: test_programs
  test_conf = configuration_data()
  test_conf.set('installed_tests_dir', installed_tests_execdir)
  test_conf.set('program', program)

  configure_file(
    input: test_template,
    output: program + '.test',
    install: enable_installed_tests,
    install_dir: installed_tests_metadir,
    configuration: test_conf,
  )

  main = files(program)
  if enable_installed_tests
    install_data(
      main,
      files('taptestrunner.py'),
      install_dir: installed_tests_execdir,
      install_mode: 'rwxr-xr-x',
    )
  endif

  test(
    program,
    py3,
    args: main,
    env: envs,
  )
endforeach
