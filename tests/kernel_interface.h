#ifndef U80211_TEST_KERNEL_INTERFACE_H
#define U80211_TEST_KERNEL_INTERFACE_H

#include <stddef.h>

#include <u80211/u80211.h>

typedef void (*receive_handler_t)(u80211_device_t *device, const void *buffer, size_t size, void *context);

void set_receive_handler(receive_handler_t handler, void *context);

#endif
