#ifndef U80211_TEST_HWSIM_H
#define U80211_TEST_HWSIM_H

#include <u80211/u80211.h>

int hwsim_open(const char *interface_name, u80211_device_t **device_out);
int hwsim_close(u80211_device_t *device);

#endif
