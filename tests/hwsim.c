#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <u80211/status.h>

#include "hwsim.h"

extern char **environ;

typedef struct {
	int packet_fd;
	int wake_fd;
	int ifindex;
	char interface_name[IF_NAMESIZE];
	pthread_t receiver_thread;
	int receiver_started;
	int receiver_error;
	u80211_device_t *device;
} hwsim_device_t;

static int status_from_errno(int error) {
	switch (error) {
	case 0:
		return U80211_STATUS_SUCCESS;
	case ENOMEM:
		return U80211_STATUS_ENOMEM;
	default:
		return U80211_STATUS_UNKNOWN_ERROR;
	}
}

static uint16_t read_le16(const uint8_t *data) {
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void clear_descriptor(u80211_tx_buffer_descriptor_t *descriptor) {
	descriptor->data = NULL;
	descriptor->size = 0;
	descriptor->current_offset = 0;
}

static int allocate_tx_buffer(u80211_device_t *device, size_t size, u80211_tx_buffer_descriptor_t *descriptor) {
	(void)device;

	void *data = malloc(size);
	if (data == NULL)
		return status_from_errno(ENOMEM);

	descriptor->data = data;
	descriptor->size = size;
	descriptor->current_offset = size;
	return U80211_STATUS_SUCCESS;
}

static int free_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor) {
	(void)device;

	free(descriptor->data);
	clear_descriptor(descriptor);
	return U80211_STATUS_SUCCESS;
}

static int transmit(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor, const u80211_transmit_options_t *options) {
	(void)options;
	int result = 0;

	hwsim_device_t *hwsim = device->driver_data;
	const uint8_t radiotap[] = {
		0x00, 0x00, 0x08, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};

	struct iovec vectors[] = {
		{ .iov_base = (void *)radiotap, .iov_len = sizeof(radiotap) },
		{
			.iov_base = (uint8_t *)descriptor->data
				+ descriptor->current_offset,
			.iov_len = descriptor->size - descriptor->current_offset,
		},
	};

	struct sockaddr_ll destination = {
		.sll_family = AF_PACKET,
		.sll_protocol = htons(ETH_P_ALL),
		.sll_ifindex = hwsim->ifindex,
	};

	struct msghdr message = {
		.msg_name = &destination,
		.msg_namelen = sizeof(destination),
		.msg_iov = vectors,
		.msg_iovlen = sizeof(vectors) / sizeof(vectors[0]),
	};

	ssize_t sent;
	do {
		sent = sendmsg(hwsim->packet_fd, &message, 0);
	} while (sent < 0 && errno == EINTR);

	if (sent < 0)
		result = errno;
	else if ((size_t)sent != sizeof(radiotap) + vectors[1].iov_len)
		result = EIO;

	free(descriptor->data);
	clear_descriptor(descriptor);
	return status_from_errno(result);
}

static int set_channel(u80211_device_t *device, int channel) {
	hwsim_device_t *hwsim = device->driver_data;
	char channel_string[16];
	snprintf(channel_string, sizeof(channel_string), "%d", channel);
	char *arguments[] = {
		"iw", "dev", hwsim->interface_name, "set", "channel", channel_string, NULL
	};
	pid_t child;
	int error = posix_spawnp(&child, "iw", NULL, NULL, arguments, environ);
	if (error != 0)
		return status_from_errno(error);

	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);

	if (waited < 0)
		return status_from_errno(errno);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return status_from_errno(EIO);

	return U80211_STATUS_SUCCESS;
}

static const u80211_device_ops_t device_ops = {
	.allocate_tx_buffer = allocate_tx_buffer,
	.free_tx_buffer = free_tx_buffer,
	.transmit = transmit,
	.set_channel = set_channel,
};

static void *receive_packets(void *argument) {
	hwsim_device_t *hwsim = argument;
	uint8_t packet[65536];
	struct pollfd descriptors[] = {
		{ .fd = hwsim->packet_fd, .events = POLLIN },
		{ .fd = hwsim->wake_fd, .events = POLLIN },
	};

	for (;;) {
		int ready;
		do {
			ready = poll(descriptors, 2, -1);
		} while (ready < 0 && errno == EINTR);
		if (ready < 0) {
			hwsim->receiver_error = errno;
			break;
		}

		if (descriptors[1].revents & POLLIN)
			break;

		if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			hwsim->receiver_error = EIO;
			break;
		}

		if (!(descriptors[0].revents & POLLIN))
			continue;

		struct sockaddr_ll source;
		socklen_t source_size = sizeof(source);
		ssize_t packet_size = recvfrom(hwsim->packet_fd, packet, sizeof(packet), 0, (struct sockaddr *)&source, &source_size);
		if (packet_size < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;

			hwsim->receiver_error = errno;
			break;
		}

		if (source.sll_pkttype == PACKET_OUTGOING || packet_size < 8)
			continue;

		size_t radiotap_size = read_le16(packet + 2);
		if (packet[0] != 0 || radiotap_size < 8 || radiotap_size > (size_t)packet_size)
			continue;

		u80211_process_packet(hwsim->device, packet + radiotap_size, (size_t)packet_size - radiotap_size);
	}

	return NULL;
}

int hwsim_open(const char *interface_name, u80211_device_t **device_out) {
	errno = 0;
	int ifindex = if_nametoindex(interface_name);
	if (ifindex == 0)
		return status_from_errno(errno != 0 ? errno : ENODEV);

	hwsim_device_t *hwsim = calloc(1, sizeof(*hwsim));
	if (hwsim == NULL)
		return status_from_errno(ENOMEM);
	hwsim->packet_fd = -1;
	hwsim->wake_fd = -1;
	hwsim->ifindex = ifindex;
	memcpy(hwsim->interface_name, interface_name, strlen(interface_name) + 1);

	hwsim->packet_fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_ALL));
	if (hwsim->packet_fd < 0) {
		int error = errno;
		free(hwsim);
		return status_from_errno(error);
	}

	struct sockaddr_ll address = {
		.sll_family = AF_PACKET,
		.sll_protocol = htons(ETH_P_ALL),
		.sll_ifindex = ifindex,
	};
	if (bind(hwsim->packet_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		int error = errno;
		close(hwsim->packet_fd);
		free(hwsim);
		return status_from_errno(error);
	}

	struct ifreq request = { 0 };
	memcpy(request.ifr_name, interface_name, strlen(interface_name) + 1);
	if (ioctl(hwsim->packet_fd, SIOCGIFHWADDR, &request) < 0) {
		int error = errno;
		close(hwsim->packet_fd);
		free(hwsim);
		return status_from_errno(error);
	}

	u80211_device_metadata_t metadata = {
		.rate_bitmap = {
			[2 / 8] = 1 << (2 % 8) | 1 << (4 % 8),
			[11 / 8] = 1 << (11 % 8) | 1 << (12 % 8),
			[18 / 8] = 1 << (18 % 8) | 1 << (22 % 8),
			[24 / 8] = 1 << (24 % 8),
			[36 / 8] = 1 << (36 % 8),
			[48 / 8] = 1 << (48 % 8),
			[72 / 8] = 1 << (72 % 8),
			[96 / 8] = 1 << (96 % 8),
			[108 / 8] = 1 << (108 % 8),
		},
	};
	memcpy(metadata.mac_address.bytes, request.ifr_hwaddr.sa_data, sizeof(metadata.mac_address.bytes));
	int error = u80211_register_device(&metadata, &device_ops, hwsim, &hwsim->device);
	if (error != 0) {
		close(hwsim->packet_fd);
		free(hwsim);
		return error;
	}

	hwsim->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (hwsim->wake_fd < 0) {
		error = errno;
		u80211_unregister_device(hwsim->device);
		close(hwsim->packet_fd);
		free(hwsim);
		return status_from_errno(error);
	}

	error = pthread_create(&hwsim->receiver_thread, NULL, receive_packets, hwsim);
	if (error != 0) {
		close(hwsim->wake_fd);
		u80211_unregister_device(hwsim->device);
		close(hwsim->packet_fd);
		free(hwsim);
		return status_from_errno(error);
	}
	hwsim->receiver_started = 1;
	*device_out = hwsim->device;
	return U80211_STATUS_SUCCESS;
}

int hwsim_close(u80211_device_t *device) {
	hwsim_device_t *hwsim = device->driver_data;

	if (hwsim->receiver_started) {
		uint64_t wake = 1;
		ssize_t written;
		written = write(hwsim->wake_fd, &wake, sizeof(wake));
		if (written < 0 && errno != EAGAIN)
			hwsim->receiver_error = errno;

		int error = pthread_join(hwsim->receiver_thread, NULL);
		if (error != 0 && hwsim->receiver_error == 0)
			hwsim->receiver_error = error;
	}

	int result = hwsim->receiver_error;
	u80211_unregister_device(device);
	close(hwsim->wake_fd);
	close(hwsim->packet_fd);
	free(hwsim);
	return status_from_errno(result);
}
