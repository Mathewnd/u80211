hello_test = executable(
  'hello-test',
  'programs/test.c',
)

test('test', hello_test)

threads = dependency('threads')

hwsim_works_test = executable(
  'hwsim-works',
  files(
    'programs/hwsim-works.c',
    'hwsim.c',
    'kernel_interface.c',
  ),
  include_directories: u80211_include,
  link_with: u80211_library,
  dependencies: threads,
)

active_scan_test = executable(
  'active-scan',
  files(
    'programs/active-scan.c',
    'hwsim.c',
    'kernel_interface.c',
  ),
  include_directories: u80211_include,
  link_with: u80211_library,
  dependencies: threads,
)

association_test = executable(
  'association',
  files(
    'programs/association.c',
    'hwsim.c',
    'kernel_interface.c',
  ),
  include_directories: u80211_include,
  link_with: u80211_library,
  dependencies: threads,
)

arp_test = executable(
  'arp',
  files(
    'programs/arp.c',
    'hwsim.c',
    'kernel_interface.c',
  ),
  include_directories: u80211_include,
  link_with: u80211_library,
  dependencies: threads,
)

hostapd = find_program('hostapd')
hostapd_cli = find_program('hostapd_cli')
wpa_supplicant = find_program('wpa_supplicant')
wpa_cli = find_program('wpa_cli')
run_with_hostapd = find_program('run-with-hostapd.sh')

test(
  'hwsim-works',
  run_with_hostapd,
  args: [
    hostapd.full_path(),
    hostapd_cli.full_path(),
    wpa_supplicant.full_path(),
    wpa_cli.full_path(),
    files('hostapd.conf.in'),
    files('wpa_supplicant.conf.in'),
    hwsim_works_test,
  ],
  is_parallel: false,
  timeout: 60,
)

test(
  'active-scan',
  run_with_hostapd,
  args: [
    hostapd.full_path(),
    hostapd_cli.full_path(),
    wpa_supplicant.full_path(),
    wpa_cli.full_path(),
    files('hostapd.conf.in'),
    files('wpa_supplicant.conf.in'),
    active_scan_test,
  ],
  is_parallel: false,
  timeout: 60,
)

test(
  'association',
  run_with_hostapd,
  args: [
    hostapd.full_path(),
    hostapd_cli.full_path(),
    wpa_supplicant.full_path(),
    wpa_cli.full_path(),
    files('hostapd.conf.in'),
    files('wpa_supplicant.conf.in'),
    association_test,
  ],
  is_parallel: false,
  timeout: 60,
)

foreach cleanup_action : ['deauthenticate', 'disassociate']
  test(
    'association-' + cleanup_action,
    run_with_hostapd,
    args: [
      hostapd.full_path(),
      hostapd_cli.full_path(),
      wpa_supplicant.full_path(),
      wpa_cli.full_path(),
      files('hostapd.conf.in'),
      files('wpa_supplicant.conf.in'),
      association_test,
      cleanup_action,
      'ap2',
    ],
    is_parallel: false,
    timeout: 60,
  )
endforeach

test(
  'association-user-disassociate',
  run_with_hostapd,
  args: [
    hostapd.full_path(),
    hostapd_cli.full_path(),
    wpa_supplicant.full_path(),
    wpa_cli.full_path(),
    files('hostapd.conf.in'),
    files('wpa_supplicant.conf.in'),
    association_test,
    'user-disassociate',
    'ap2',
  ],
  is_parallel: false,
  timeout: 60,
)

test(
  'arp',
  run_with_hostapd,
  args: [
    hostapd.full_path(),
    hostapd_cli.full_path(),
    wpa_supplicant.full_path(),
    wpa_cli.full_path(),
    files('hostapd.conf.in'),
    files('wpa_supplicant.conf.in'),
    arp_test,
  ],
  is_parallel: false,
  timeout: 60,
)
