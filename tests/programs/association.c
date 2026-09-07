#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <u80211/kernel_interface.h>
#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"

static void wait_for_delayed_association_work(void) {
	unsigned int seconds = 6;
	while (seconds != 0)
		seconds = sleep(seconds);
}

static int format_station_address(u80211_device_t *device, char station_address[18]) {
	int length = snprintf(station_address, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		device->metadata.mac_address.bytes[0], device->metadata.mac_address.bytes[1],
		device->metadata.mac_address.bytes[2], device->metadata.mac_address.bytes[3],
		device->metadata.mac_address.bytes[4], device->metadata.mac_address.bytes[5]);
	return length == 17 ? 0 : -1;
}

static int run_ap_cleanup_command(u80211_device_t *device, const char *action, const char *interface) {
	const char *hostapd_cli = getenv("U80211_HOSTAPD_CLI");
	const char *hostapd_control = getenv("U80211_HOSTAPD_CONTROL");
	if (hostapd_cli == NULL || hostapd_control == NULL) {
		fputs("hostapd control environment is unavailable\n", stderr);
		return -1;
	}

	char station_address[18];
	if (format_station_address(device, station_address) != 0)
		return -1;

	pid_t child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		execl(hostapd_cli, hostapd_cli, "-p", hostapd_control, "-i", interface, action, station_address, (char *)NULL);
		_exit(127);
	}

	int child_status;
	while (waitpid(child, &child_status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	return WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 ? 0 : -1;
}

static int hostapd_station_present(u80211_device_t *device, const char *interface, bool *present) {
	const char *hostapd_cli = getenv("U80211_HOSTAPD_CLI");
	const char *hostapd_control = getenv("U80211_HOSTAPD_CONTROL");
	if (hostapd_cli == NULL || hostapd_control == NULL)
		return -1;

	char station_address[18];
	if (format_station_address(device, station_address) != 0)
		return -1;

	int output_pipe[2];
	if (pipe(output_pipe) != 0)
		return -1;

	pid_t child = fork();
	if (child < 0) {
		close(output_pipe[0]);
		close(output_pipe[1]);
		return -1;
	}
	if (child == 0) {
		close(output_pipe[0]);
		if (dup2(output_pipe[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(output_pipe[1]);
		execl(hostapd_cli, hostapd_cli, "-p", hostapd_control, "-i", interface, "list_sta", (char *)NULL);
		_exit(127);
	}

	close(output_pipe[1]);
	char output[4096];
	size_t output_size = 0;
	while (output_size < sizeof(output) - 1) {
		ssize_t count = read(output_pipe[0], output + output_size, sizeof(output) - 1 - output_size);
		if (count > 0) {
			output_size += (size_t)count;
			continue;
		}
		if (count == 0)
			break;
		if (errno != EINTR) {
			close(output_pipe[0]);
			waitpid(child, NULL, 0);
			return -1;
		}
	}
	close(output_pipe[0]);
	output[output_size] = '\0';

	int child_status;
	while (waitpid(child, &child_status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
		return -1;

	*present = strstr(output, station_address) != NULL;
	return 0;
}

static bool wait_for_hostapd_station_removal(u80211_device_t *device, const char *interface) {
	const struct timespec interval = {
		.tv_nsec = 100000000,
	};

	for (int attempt = 0; attempt < 50; ++attempt) {
		bool present;
		if (hostapd_station_present(device, interface, &present) != 0)
			return false;
		if (!present)
			return true;

		struct timespec remaining = interval;
		while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR)
			;
	}

	return false;
}

static bool wait_for_ap_cleanup(u80211_device_t *device) {
	const struct timespec interval = {
		.tv_nsec = 100000000,
	};

	for (int attempt = 0; attempt < 50; ++attempt) {
		u80211_kernel_acquire_spinlock(device->association_spinlock);
		bool complete = u80211_get_device_state(device) == U80211_DEVICE_STATE_DOWN &&
			device->ap == NULL && device->disconnected_ap == NULL;
		u80211_kernel_release_spinlock(device->association_spinlock);
		if (complete)
			return true;

		struct timespec remaining = interval;
		while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR)
			;
	}

	return false;
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

int main(int argc, char **argv) {
	if (argc != 1 && argc != 3) {
		fprintf(stderr, "usage: %s [deauthenticate|disassociate|user-disassociate AP_INTERFACE]\n", argv[0]);
		return 2;
	}
	if (argc == 3 && strcmp(argv[1], "deauthenticate") != 0 && strcmp(argv[1], "disassociate") != 0 && strcmp(argv[1], "user-disassociate") != 0) {
		fprintf(stderr, "unsupported AP cleanup action: %s\n", argv[1]);
		return 2;
	}

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

	status = u80211_associate(device, cat_cafe, NULL, 0);
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
	if (argc == 3) {
		if (strcmp(argv[1], "user-disassociate") == 0) {
			status = u80211_disassociate(device);
			if (status != U80211_STATUS_SUCCESS) {
				fprintf(stderr, "user disassociation failed: status %d\n", status);
				goto close_device;
			}
			if (!wait_for_hostapd_station_removal(device, argv[2])) {
				fputs("hostapd retained sta0 after user disassociation\n", stderr);
				goto close_device;
			}
			status = u80211_disassociate(device);
			if (status != U80211_STATUS_NOT_ASSOCIATED) {
				fprintf(stderr, "second user disassociation returned status %d\n", status);
				goto close_device;
			}
		} else if (run_ap_cleanup_command(device, argv[1], argv[2]) != 0) {
			fprintf(stderr, "hostapd failed to %s sta0\n", argv[1]);
			goto close_device;
		}
		if (!wait_for_ap_cleanup(device)) {
			fprintf(stderr, "%s did not clean up the association\n", argv[1]);
			goto close_device;
		}
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

	if (result == 0) {
		if (argc == 3)
			printf("%s cleaned up the Cat Cafe association\n", argv[1]);
		else
			puts("associated to the Cat Cafe BSSID");
	}
	return result;
}
