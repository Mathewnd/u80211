#include <u80211/status.h>
#include <u80211/kernel_interface.h>
#include <u80211/u80211.h>

int u80211_register_device(const u80211_mac_address_t *mac_address, const u80211_device_ops_t *ops, void *driver_data, u80211_device_t **device_out) {
	u80211_device_t *device = u80211_kernel_allocate(sizeof(*device));
	if (device == NULL)
		return U80211_STATUS_ENOMEM;

	device->mac_address = *mac_address;
	device->ops = ops;
	device->driver_data = driver_data;
	*device_out = device;
	return U80211_STATUS_SUCCESS;
}

void u80211_unregister_device(u80211_device_t *device) {
	u80211_kernel_free(device);
}
