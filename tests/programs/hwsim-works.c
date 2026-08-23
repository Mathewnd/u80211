#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"

static atomic_size_t received_packets;

void u80211_process_packet(u80211_device_t *device, const void *packet, size_t packet_size) {
	(void)device;
	(void)packet;
	(void)packet_size;
	atomic_fetch_add_explicit(&received_packets, 1, memory_order_relaxed);
}

int main(void) {
	u80211_device_t *device;
	int status = hwsim_open("sta0", &device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not open sta0: status %d\n", status);
		return 1;
	}

	puts("listening on sta0 for 5 seconds...");
	sleep(5);

	status = hwsim_close(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not close sta0: status %d\n", status);
		return 1;
	}

	size_t count = atomic_load_explicit(&received_packets, memory_order_relaxed);
	printf("received %zu 802.11 packets\n", count);
	return count == 0;
}
