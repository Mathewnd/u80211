#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"

typedef struct {
	const char *ssid;
	uint8_t channel;
	bool found;
} expected_ap_t;

static int validate_scan_results(u80211_device_t *device) {
	expected_ap_t expected[] = {
		{ .ssid = "Foo University", .channel = 1 },
		{ .ssid = "My House", .channel = 1 },
		{ .ssid = "Cat Cafe", .channel = 6 },
		{ .ssid = "Bar Park", .channel = 11 },
	};
	const size_t expected_count = sizeof(expected) / sizeof(expected[0]);
	size_t cache_count = u80211_bss_cache_get_count(&device->bss_cache);
	bool valid = cache_count == expected_count;
	if (!valid)
		fprintf(stderr, "expected %zu access points, found %zu\n", expected_count, cache_count);

	u80211_ap_t *aps[sizeof(expected) / sizeof(expected[0])];
	size_t ap_count = u80211_bss_cache_get_aps(&device->bss_cache, aps, expected_count);
	if (ap_count != cache_count) {
		fprintf(stderr, "expected to read %zu access points, read %zu\n", cache_count, ap_count);
		for (size_t i = 0; i < ap_count; ++i)
			u80211_ap_release(aps[i]);
		return 1;
	}

	for (size_t i = 0; i < ap_count; ++i) {
		size_t match = expected_count;
		for (size_t j = 0; j < expected_count; ++j) {
			if (strcmp(aps[i]->ssid, expected[j].ssid) == 0 && aps[i]->channel == expected[j].channel) {
				match = j;
				break;
			}
		}

		if (match == expected_count) {
			fprintf(stderr, "unexpected access point '%s' on channel %u\n", aps[i]->ssid, (unsigned)aps[i]->channel);
			valid = false;
		} else if (expected[match].found) {
			fprintf(stderr, "duplicate access point '%s' on channel %u\n", aps[i]->ssid, (unsigned)aps[i]->channel);
			valid = false;
		} else {
			expected[match].found = true;
		}

		u80211_ap_release(aps[i]);
	}

	for (size_t i = 0; i < expected_count; ++i) {
		if (!expected[i].found) {
			fprintf(stderr, "missing access point '%s' on channel %u\n", expected[i].ssid, (unsigned)expected[i].channel);
			valid = false;
		}
	}

	return valid ? 0 : 1;
}

int main(void) {
	u80211_device_t *device;
	int status = hwsim_open("sta0", &device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not open sta0: status %d\n", status);
		return 1;
	}

	int result = 1;
	status = u80211_scan(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not start active scan: status %d\n", status);
		goto close_device;
	}

	status = u80211_wait_for_scan_completion(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "active scan completion wait failed: status %d\n", status);
		goto close_device;
	}

	if (validate_scan_results(device) != 0)
		goto close_device;

	result = 0;

close_device:
	status = hwsim_close(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not close sta0: status %d\n", status);
		result = 1;
	}

	if (result == 0)
		puts("active scan discovered all configured access points");
	return result;
}
