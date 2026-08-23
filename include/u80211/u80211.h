#ifndef U80211_U80211_H
#define U80211_U80211_H

#include <stddef.h>
#include <u80211/header.h>

typedef struct u80211_device u80211_device_t;

typedef struct {
	void *data;
	size_t size;
	size_t current_offset;
} u80211_tx_buffer_descriptor_t;

typedef struct {
	int (*allocate_tx_buffer)(u80211_device_t *device, size_t size,
			u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*free_tx_buffer)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*transmit)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*set_channel)(u80211_device_t *device, int channel);
} u80211_device_ops_t;

struct u80211_device {
	u80211_mac_address_t mac_address;
	const u80211_device_ops_t *ops;
	void *driver_data;
};

void u80211_process_packet(u80211_device_t *device, const void *packet, size_t packet_size);

int u80211_register_device(const u80211_mac_address_t *mac_address, const u80211_device_ops_t *ops, void *driver_data, u80211_device_t **device_out);
void u80211_unregister_device(u80211_device_t *device);

#endif
