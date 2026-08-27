#ifndef U80211_U80211_H
#define U80211_U80211_H

#include <stdbool.h>
#include <stddef.h>
#include <u80211/bss_cache.h>
#include <u80211/list.h>
#include <u80211/packet.h>

static inline void *u80211_descriptor_allocate_space(u80211_tx_buffer_descriptor_t *descriptor, size_t size) {
	if (size > descriptor->current_offset)
		return NULL;

	descriptor->current_offset -= size;
	return (uint8_t *)descriptor->data + descriptor->current_offset;
}

typedef struct {
	int (*allocate_tx_buffer)(u80211_device_t *device, size_t size, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*free_tx_buffer)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*transmit)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*set_channel)(u80211_device_t *device, int channel);
} u80211_device_ops_t;

typedef struct {
	u80211_mac_address_t mac_address;
	uint8_t rate_bitmap[16];
} u80211_device_metadata_t;

#define U80211_DEVICE_STATE_DOWN 0
#define U80211_DEVICE_STATE_SCANNING 1
#define U80211_DEVICE_STATE_AUTHENTICATING 2
#define U80211_DEVICE_STATE_ASSOCIATING 3
#define U80211_DEVICE_STATE_ASSOCIATED 4
struct u80211_device {
	u80211_device_metadata_t metadata;
	void *driver_data;
	const u80211_device_ops_t *ops;

	int state;
	size_t packet_count;

	void *scan_spinlock;
	void *scan_context;
	u80211_list_t scan_waiters;

	bss_cache_t bss_cache;
};

static inline int u80211_get_device_state(u80211_device_t *device) {
	return __atomic_load_n(&device->state, __ATOMIC_ACQUIRE);
}

static inline bool u80211_set_device_state(u80211_device_t *device, int old_state, int new_state) {
	return __atomic_compare_exchange_n(&device->state, &old_state, new_state, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

int u80211_scan(u80211_device_t *device);
int u80211_wait_for_scan_completion(u80211_device_t *device);

void u80211_process_packet(u80211_device_t *device, const void *packet, size_t packet_size);

// these are mostly diagnostic and should not be relied upon
size_t u80211_get_packet_count(u80211_device_t *device);
void u80211_reset_packet_count(u80211_device_t *device);

int u80211_register_device(const u80211_device_metadata_t *metadata, const u80211_device_ops_t *ops, void *driver_data, u80211_device_t **device_out);
void u80211_unregister_device(u80211_device_t *device);

#endif
