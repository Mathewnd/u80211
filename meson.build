project(
  'u80211',
  'c',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

u80211_include = include_directories('include')

u80211_test_library = static_library(
  'u80211-test',
  files(
    'src/rbtree.c',
    'src/bss_cache.c',
    'src/ap.c',
    'tests/kernel_interface.c',
  ),
  include_directories: u80211_include,
)

sh = find_program('sh')
hostapd = find_program('hostapd')
hostapd_cli = find_program('hostapd_cli')

add_test_setup(
  'hostapd',
  exe_wrapper: [
    sh,
    join_paths(meson.current_source_dir(), 'tests', 'run-with-hostapd.sh'),
    hostapd,
    hostapd_cli,
    join_paths(meson.current_source_dir(), 'tests', 'hostapd.conf.in'),
  ],
  is_default: true,
)

test_programs = [
  'test',
]

foreach test_program : test_programs
  test_executable = executable(
    'u80211-' + test_program,
    join_paths('tests', 'programs', test_program + '.c'),
    include_directories: u80211_include,
    link_with: u80211_test_library,
  )

  test(
    test_program,
    test_executable,
    is_parallel: false,
  )
endforeach
