#include <u80211/u80211.h>
#include <u80211/packet.h>
#include <u80211/kernel_interface.h>
#include <u80211/bss_cache.h>
#include <u80211/status.h>
#include <stdbool.h>

#define WAIT_MS 75
#define BUFFER_COUNT 64

typedef struct {
	u80211_beacon_data_t buffer[BUFFER_COUNT];
	u80211_device_t *device;
	bool receiving;
	size_t received;
	int current_channel;
	void *work;
} u80211_scan_state_t;

static void free_scan_state(u80211_scan_state_t *scan_state) {
	scan_state->device->scan_context = NULL;
	u80211_kernel_free_work(scan_state->work);
	u80211_kernel_free(scan_state);
}
#define U80211_CHANNEL_RULES_DISABLED 1 // no receive/transmission.
#define U80211_CHANNEL_RULES_PASSIVE 2 // only transmit when a passive scan returns an AP in that channel.
typedef struct {
	int flags;
	int max_dbm;
} u80211_channel_rules_t;

u80211_channel_rules_t u80211_get_channel_rules(int channel);

// TODO: this obviously won't work for anything other than 2.4ghz wifi
static int next_channel(int current) {
	for (int channel = current + 1; current <= 14; ++current) {
		if (u80211_get_channel_rules(channel).flags & (U80211_CHANNEL_RULES_DISABLED | U80211_CHANNEL_RULES_PASSIVE))
			continue;

		return channel;
	}

	return -1;
}

static void scan_work(void *ctx) {
	u80211_scan_state_t *scan_state = ctx;
	u80211_device_t *device = scan_state->device;

	__atomic_store_n(&scan_state->receiving, false, __ATOMIC_RELAXED);
	size_t received = __atomic_load_n(&scan_state->received, __ATOMIC_ACQUIRE);

	for (size_t i = 0; i < received; ++i) {
		// TODO: for each received in buffer, add to bss cache a new description
	}

	scan_state->current_channel = next_channel(scan_state->current_channel);
	if (scan_state->current_channel < 0) {
		free_scan_state(scan_state);
		__atomic_store_n(&device->state, U80211_DEVICE_STATE_DOWN, __ATOMIC_RELEASE);
		// TODO: signal state-change event
		return;
	}

	__atomic_store_n(&scan_state->received, 0, __ATOMIC_RELAXED);

	device->ops->set_channel(device, scan_state->current_channel);
	__atomic_store_n(&scan_state->receiving, true, __ATOMIC_RELEASE);
	// TODO send packet
	u80211_kernel_enqueue_work(scan_state->work, scan_work, scan_state, WAIT_MS);
}

int u80211_scan(u80211_device_t *device) {
	u80211_scan_state_t *scan_state = u80211_kernel_allocate(sizeof(u80211_scan_state_t));
	if (scan_state == NULL)
		return U80211_STATUS_ENOMEM;

	scan_state->work = u80211_kernel_allocate_work();
	if (scan_state->work == NULL) {
		u80211_kernel_free(scan_state);
		return U80211_STATUS_ENOMEM;
	}

	__atomic_store_n(&scan_state->received, 0, __ATOMIC_RELAXED);
	scan_state->device = device;
	scan_state->current_channel = next_channel(0);
	if (scan_state->current_channel < 0) {
		free_scan_state(scan_state);
		return U80211_STATUS_NOT_PERMITTED;
	}

	int expected = U80211_DEVICE_STATE_DOWN;
	if (__atomic_compare_exchange_n(&device->state, &expected, U80211_DEVICE_STATE_SCANNING, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
		free_scan_state(scan_state);
		return U80211_STATUS_BUSY;
	}

	u80211_bss_cache_purge();

	device->ops->set_channel(device, scan_state->current_channel);
	__atomic_store_n(&scan_state->receiving, true, __ATOMIC_RELEASE);
	// TODO send packet
	u80211_kernel_enqueue_work(scan_state->work, scan_work, scan_state, WAIT_MS);

	return U80211_STATUS_SUCCESS;
}
