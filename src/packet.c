#include <u80211/packet.h>
#include <u80211/u80211.h>

void u80211_process_packet(u80211_device_t *device, const void *packet, size_t packet_size) {
	__atomic_add_fetch(&device->packet_count, 1, __ATOMIC_RELAXED);
}

size_t u80211_get_packet_count(u80211_device_t *device) {
	return __atomic_load_n(&device->packet_count, __ATOMIC_RELAXED);
}

void u80211_reset_packet_count(u80211_device_t *device) {
	__atomic_store_n(&device->packet_count, 0, __ATOMIC_RELAXED);
}
