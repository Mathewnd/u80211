# u80211

u80211 is a portable implementation of a 802.11 layer.

This is still experimental, so use with caution.

Current limitations:
- Open networks and WPA2 (TKIP + CCMP) only
- No rate selection. Currently locked to 1mbps.
- No API for regulation selection (channels 1 to 11 only by default)
- Hardware encryption only. Expects specific behavior from the NICs as well (such as not stripping headers).
- Active scanning only

# Hardware drivers

Easy-to-integrate NIC drivers are available in the [u80211_drv repository](https://github.com/mathewnd/u80211_drv).

# How to port

1. Add the sources under src/ to your build
2. Add the headers under include/ to your build
3. Implement the kernel API declared in [`include/u80211/kernel_interface.h`](include/u80211/kernel_interface.h)

# Active scan example

```c
#include <stdio.h>

#include <u80211/u80211.h>

int scan_and_print(u80211_device_t *device) {
    int status = u80211_scan(device);
    if (status != U80211_STATUS_SUCCESS)
        return status;

    status = u80211_wait_for_scan_completion(device);
    if (status != U80211_STATUS_SUCCESS)
        return status;

    u80211_ap_t *access_points[64];
    size_t count = u80211_bss_cache_get_aps(
        &device->bss_cache, access_points,
        sizeof(access_points) / sizeof(access_points[0]));

    for (size_t i = 0; i < count; ++i) {
        u80211_ap_t *ap = access_points[i];
        printf("%s (channel %u)\n", ap->ssid, (unsigned int)ap->channel);
        u80211_ap_release(ap);
    }

    return U80211_STATUS_SUCCESS;
}
```
