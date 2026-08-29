#include <u80211/status.h>
#include <u80211/kernel_interface.h>
#include <u80211/u80211.h>

int u80211_register_device(const u80211_device_metadata_t *metadata, const u80211_device_ops_t *ops, void *driver_data, u80211_device_t **device_out) {
	u80211_device_t *device = u80211_kernel_allocate(sizeof(*device));
	if (device == NULL)
		return U80211_STATUS_ENOMEM;

	device->metadata = *metadata;
	device->packet_count = 0;
	device->state = U80211_DEVICE_STATE_DOWN;

	device->scan_spinlock = u80211_kernel_allocate_spinlock();
	if (device->scan_spinlock == NULL) {
		u80211_kernel_free(device);
		return U80211_STATUS_ENOMEM;
	}
	device->scan_context = NULL;
	u80211_list_init(&device->scan_waiters);

	device->association_spinlock = u80211_kernel_allocate_spinlock();
	if (device->association_spinlock == NULL) {
		u80211_kernel_free_spinlock(device->scan_spinlock);
		u80211_kernel_free(device);
		return U80211_STATUS_ENOMEM;
	}
	device->association_context = NULL;
	u80211_list_init(&device->association_waiters);
	device->association_generation = 0;
	device->association_result = U80211_STATUS_UNKNOWN_ERROR;
	device->ap = NULL;

	device->ops = ops;
	device->driver_data = driver_data;

	int status = u80211_bss_cache_init(&device->bss_cache);
	if (status != U80211_STATUS_SUCCESS) {
		u80211_kernel_free_spinlock(device->association_spinlock);
		u80211_kernel_free_spinlock(device->scan_spinlock);
		u80211_kernel_free(device);
		return status;
	}

	*device_out = device;
	return U80211_STATUS_SUCCESS;
}

void u80211_unregister_device(u80211_device_t *device) {
	u80211_bss_cache_deinit(&device->bss_cache);
	u80211_kernel_free_spinlock(device->association_spinlock);
	u80211_kernel_free_spinlock(device->scan_spinlock);
	u80211_kernel_free(device);
}
