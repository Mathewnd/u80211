project(
  'u80211',
  'c',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

u80211_include = include_directories('include')

u80211_library = static_library(
  'u80211',
  files(
    'src/device.c',
    'src/packet.c',
    'src/string.c',
    'src/rbtree.c',
    'src/bss_cache.c',
    'src/ap.c',
    'src/regulatory_database.c',
  ),
  include_directories: u80211_include,
)

subdir('tests')
