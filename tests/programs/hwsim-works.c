#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"

int main(void) {
	u80211_device_t *device;
	int status = hwsim_open("sta0", &device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not open sta0: status %d\n", status);
		return 1;
	}

	puts("listening on sta0 for 5 seconds...");
	sleep(5);
	size_t count = u80211_get_packet_count(device);

	status = hwsim_close(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not close sta0: status %d\n", status);
		return 1;
	}

	printf("received %zu 802.11 packets\n", count);
	return count == 0;
}
