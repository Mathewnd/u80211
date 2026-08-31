#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <u80211/status.h>
#include <u80211/u80211.h>

#include "../hwsim.h"
#include "../kernel_interface.h"

#define ARP_FRAME_SIZE 42
#define ARP_REPLY_TIMEOUT_MILLISECONDS 5000
#define ARP_RETRY_INTERVAL_MILLISECONDS 100
#define ARP_PROBE_COUNT (ARP_REPLY_TIMEOUT_MILLISECONDS / ARP_RETRY_INTERVAL_MILLISECONDS)

static const uint8_t sta0_ip[] = { 192, 0, 2, 1 };
static const uint8_t sta1_ip[] = { 192, 0, 2, 2 };

typedef struct {
	u80211_device_t *device;
	u80211_mac_address_t local_mac;
	sem_t received;
	atomic_uint arp_frames_seen;
	atomic_uint request_echoes_seen;
} arp_waiter_t;

static uint16_t read_be16(const uint8_t *data) {
	return ((uint16_t)data[0] << 8) | data[1];
}

static void write_be16(uint8_t *data, uint16_t value) {
	data[0] = (uint8_t)(value >> 8);
	data[1] = (uint8_t)value;
}

static void wait_for_delayed_association_work(void) {
	unsigned int seconds = 6;
	while (seconds != 0)
		seconds = sleep(seconds);
}

static u80211_ap_t *find_foo_university(u80211_device_t *device) {
	size_t capacity = u80211_bss_cache_get_count(&device->bss_cache);
	if (capacity == 0)
		return NULL;

	u80211_ap_t **aps = malloc(sizeof(*aps) * capacity);
	if (aps == NULL)
		return NULL;

	size_t count = u80211_bss_cache_get_aps(&device->bss_cache, aps, capacity);
	u80211_ap_t *foo_university = NULL;
	for (size_t i = 0; i < count; ++i) {
		if (foo_university == NULL && strcmp(aps[i]->ssid, "Foo University") == 0)
			foo_university = aps[i];
		else
			u80211_ap_release(aps[i]);
	}

	free(aps);
	return foo_university;
}

static void receive_arp_reply(u80211_device_t *device, const void *buffer, size_t size, void *context) {
	arp_waiter_t *waiter = context;
	const uint8_t *frame = buffer;
	if (device != waiter->device || size < ARP_FRAME_SIZE)
		return;

	const uint8_t *arp = frame + 14;
	if (read_be16(frame + 12) != 0x0806
			|| read_be16(arp) != 1
			|| read_be16(arp + 2) != 0x0800
			|| arp[4] != 6
			|| arp[5] != 4)
		return;

	atomic_fetch_add_explicit(&waiter->arp_frames_seen, 1, memory_order_relaxed);
	if (read_be16(arp + 6) == 1
			&& memcmp(frame, "\xff\xff\xff\xff\xff\xff", 6) == 0
			&& memcmp(frame + 6, waiter->local_mac.bytes, sizeof(waiter->local_mac.bytes)) == 0
			&& memcmp(arp + 14, sta0_ip, sizeof(sta0_ip)) == 0
			&& memcmp(arp + 24, sta1_ip, sizeof(sta1_ip)) == 0) {
		atomic_fetch_add_explicit(&waiter->request_echoes_seen, 1, memory_order_relaxed);
		return;
	}

	if (read_be16(arp + 6) != 2
			|| memcmp(frame, waiter->local_mac.bytes, sizeof(waiter->local_mac.bytes)) != 0
			|| memcmp(frame + 6, arp + 8, 6) != 0
			|| memcmp(arp + 14, sta1_ip, sizeof(sta1_ip)) != 0
			|| memcmp(arp + 18, waiter->local_mac.bytes, sizeof(waiter->local_mac.bytes)) != 0
			|| memcmp(arp + 24, sta0_ip, sizeof(sta0_ip)) != 0)
		return;

	sem_post(&waiter->received);
}

static int send_arp_request(u80211_device_t *device) {
	u80211_tx_buffer_descriptor_t descriptor;
	int status = u80211_allocate_tx_buffer(device, &descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	uint8_t *frame = u80211_descriptor_allocate_space(&descriptor, ARP_FRAME_SIZE);
	if (frame == NULL)
		return u80211_transmit_buffer(device, &descriptor);

	memset(frame, 0, ARP_FRAME_SIZE);
	memset(frame, 0xff, 6);
	memcpy(frame + 6, device->metadata.mac_address.bytes, sizeof(device->metadata.mac_address.bytes));
	write_be16(frame + 12, 0x0806);

	uint8_t *arp = frame + 14;
	write_be16(arp, 1);
	write_be16(arp + 2, 0x0800);
	arp[4] = 6;
	arp[5] = 4;
	write_be16(arp + 6, 1);
	memcpy(arp + 8, device->metadata.mac_address.bytes, sizeof(device->metadata.mac_address.bytes));
	memcpy(arp + 14, sta0_ip, sizeof(sta0_ip));
	memcpy(arp + 24, sta1_ip, sizeof(sta1_ip));

	return u80211_transmit_buffer(device, &descriptor);
}

static int wait_for_arp_reply(arp_waiter_t *waiter, unsigned int milliseconds) {
	struct timespec deadline;
	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		return errno;
	deadline.tv_sec += milliseconds / 1000;
	deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}

	for (;;) {
		if (sem_timedwait(&waiter->received, &deadline) == 0)
			return 0;
		if (errno != EINTR)
			return errno;
	}
}

int main(void) {
	arp_waiter_t waiter = { 0 };
	if (sem_init(&waiter.received, 0, 0) != 0) {
		perror("could not initialize ARP reply semaphore");
		return 1;
	}

	u80211_device_t *device;
	int status = hwsim_open("sta0", &device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not open sta0: status %d\n", status);
		sem_destroy(&waiter.received);
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

	u80211_ap_t *foo_university = find_foo_university(device);
	if (foo_university == NULL) {
		fputs("could not find the Foo University BSSID\n", stderr);
		goto close_device;
	}

	status = u80211_associate(device, foo_university);
	u80211_ap_release(foo_university);
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

	waiter.device = device;
	waiter.local_mac = device->metadata.mac_address;
	set_receive_handler(receive_arp_reply, &waiter);

	unsigned int probes_sent = 0;
	int error = ETIMEDOUT;
	while (probes_sent < ARP_PROBE_COUNT) {
		status = send_arp_request(device);
		if (status != U80211_STATUS_SUCCESS) {
			fprintf(stderr, "could not send ARP request: status %d\n", status);
			goto close_device;
		}
		probes_sent++;

		error = wait_for_arp_reply(&waiter, ARP_RETRY_INTERVAL_MILLISECONDS);
		if (error != ETIMEDOUT)
			break;
	}

	if (error != 0) {
		if (error == ETIMEDOUT) {
			fprintf(stderr,
				"timed out waiting for ARP reply from 192.0.2.2 after %u probes; "
				"sta0 received %u ARP frames including %u request echoes\n",
				probes_sent,
				atomic_load_explicit(&waiter.arp_frames_seen, memory_order_relaxed),
				atomic_load_explicit(&waiter.request_echoes_seen, memory_order_relaxed));
		} else {
			errno = error;
			perror("could not wait for ARP reply");
		}
		goto close_device;
	}

	result = 0;

close_device:
	set_receive_handler(NULL, NULL);
	if (association_started)
		wait_for_delayed_association_work();

	status = hwsim_close(device);
	if (status != U80211_STATUS_SUCCESS) {
		fprintf(stderr, "could not close sta0: status %d\n", status);
		result = 1;
	}
	sem_destroy(&waiter.received);

	if (result == 0)
		puts("received ARP reply from 192.0.2.2");
	return result;
}
