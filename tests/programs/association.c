#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"

static void wait_for_delayed_association_work(void) {
	unsigned int seconds = 6;
	while (seconds != 0)
		seconds = sleep(seconds);
}

static u80211_ap_t *find_cat_cafe(u80211_device_t *device) {
	size_t capacity = u80211_bss_cache_get_count(&device->bss_cache);
	if (capacity == 0)
		return NULL;

	u80211_ap_t **aps = malloc(sizeof(*aps) * capacity);
	if (aps == NULL)
		return NULL;

	size_t count = u80211_bss_cache_get_aps(&device->bss_cache, aps, capacity);
	u80211_ap_t *cat_cafe = NULL;
	for (size_t i = 0; i < count; ++i) {
		if (cat_cafe == NULL && strcmp(aps[i]->ssid, "Cat Cafe") == 0)
			cat_cafe = aps[i];
		else
			u80211_ap_release(aps[i]);
	}

	free(aps);
	return cat_cafe;
}

int main(void) {
	u80211_device_t *device;
	int status = hwsim_open("sta0", &device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not open sta0: status %d\n", status);
		return 1;
	}

	int result = 1;
	bool association_started = false;
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

	u80211_ap_t *cat_cafe = find_cat_cafe(device);
	if (cat_cafe == NULL) {
		fputs("could not find the Cat Cafe BSSID\n", stderr);
		goto close_device;
	}
	u80211_mac_address_t cat_cafe_bssid = cat_cafe->mac_address;

	status = u80211_associate(device, cat_cafe);
	u80211_ap_release(cat_cafe);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not start association: status %d\n", status);
		goto close_device;
	}
	association_started = true;

	status = u80211_wait_for_association_completion(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "association completion wait failed: status %d\n", status);
		goto close_device;
	}

	if (u80211_get_device_state(device) != U80211_DEVICE_STATE_ASSOCIATED || device->ap == NULL || !u80211_mac_address_equal(&device->ap->mac_address, &cat_cafe_bssid)) {
		fputs("device did not associate to the Cat Cafe BSSID\n", stderr);
		goto close_device;
	}

	result = 0;

close_device:
	// Let the delayed association timeout work retire before unregistering the device.
	if (association_started)
		wait_for_delayed_association_work();

	status = hwsim_close(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not close sta0: status %d\n", status);
		result = 1;
	}

	if (result == 0)
		puts("associated to the Cat Cafe BSSID");
	return result;
}
