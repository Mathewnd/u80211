#include <stdbool.h>

#include <u80211/packet.h>
#include <u80211/scan.h>
#include <u80211/association.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

#define IE_ID_SSID 0
#define IE_ID_RATES 1
#define IE_ID_CHANNEL 3
#define IE_ID_RATES_EXT 50

#define MANAGEMENT_HEADER_SIZE 24
#define MAX_RATE_VALUE 125

// TODO check if basic rates are supported by the device
static bool handle_rates(u80211_beacon_data_t *beacon_data, uint8_t *rates, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		uint8_t rate = rates[i] & 0x7f;

		// these two have special meanings, so ignore them
		if (rate == 126 || rate == 127)
			continue;

		beacon_data->rate_bitmap[rate / 8] |= 1 << (rate % 8);
	}

	return true;
}

static void process_probe_response(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < 12)
		return;

	if (!u80211_mac_address_equal(&header->addresses[1], &header->addresses[2]) ||
			!u80211_mac_address_equal(&header->addresses[0], &device->metadata.mac_address))
		return;

	u80211_beacon_data_t beacon_data;
	u80211_memset(&beacon_data, 0, sizeof(beacon_data));
	beacon_data.interval = deserialize_le16((const void *)((uintptr_t)data + 8));
	beacon_data.capabilities = deserialize_le16((const void *)((uintptr_t)data + 10));
	beacon_data.mac_address = header->addresses[2];

	size_t ie_size = data_size - 12;
	size_t ie_offset = 0;
	while (ie_size - ie_offset) {
		if (ie_size - ie_offset < 2)
			return;

		const void *ie_base = (const void *)((uintptr_t)data + 12 + ie_offset);

		uint8_t id;
		uint8_t size;
		u80211_memcpy(&id, ie_base, 1);
		u80211_memcpy(&size, (const void *)((uintptr_t)ie_base + 1), 1);

		if (ie_size - ie_offset - 2 < size)
			return;

		switch (id) {
			case IE_ID_SSID:
				if (size > 32)
					return;

				u80211_memcpy(beacon_data.ssid, (const void *)((uintptr_t)ie_base + 2), size);
				beacon_data.ssid[size] = '\0';
				break;
			case IE_ID_CHANNEL:
				if (size != 1)
					return;

				u80211_memcpy(&beacon_data.channel, (const void *)((uintptr_t)ie_base + 2), 1);
				break;
			case IE_ID_RATES:
				if (size > 8)
					return;
				/* fall through */
			case IE_ID_RATES_EXT: {
				size_t offset = 0;
				uint8_t buffer[32];
				while (offset < size) {
					size_t count = min(sizeof(buffer), size - offset);
					u80211_memcpy(buffer, (const void *)((uintptr_t)ie_base + 2 + offset), count);
					if (!handle_rates(&beacon_data, buffer, count))
						return;

					offset += count;
				}
			}
				break;
		}

		ie_offset += size + 2;
	}

	u80211_scan_process_response(device, &beacon_data);
}

static void process_auth_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < 6)
		return;

	if (!u80211_mac_address_equal(&header->addresses[1], &header->addresses[2]) ||
			!u80211_mac_address_equal(&header->addresses[0], &device->metadata.mac_address))
		return;

	u80211_auth_data_t auth_data;
	auth_data.address = header->addresses[2];
	auth_data.auth_algorithm = deserialize_le16(data);
	auth_data.auth_transaction = deserialize_le16((const void *)((uintptr_t)data + 2));
	auth_data.status = deserialize_le16((const void *)((uintptr_t)data + 4));

	u80211_association_process_authentication(device, &auth_data);
}

void u80211_process_management_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	switch (subtype) {
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_RESPONSE:
			process_probe_response(device, header, data, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_AUTHENTICATION:
			process_auth_packet(device, header, data, data_size);
			break;
	}
}

int u80211_send_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data) {
	u80211_tx_buffer_descriptor_t descriptor;
	int status = device->ops->allocate_tx_buffer(device, MANAGEMENT_HEADER_SIZE + 6, &descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	uint8_t *data = u80211_descriptor_allocate_space(&descriptor, 6);
	if (data == NULL) {
		device->ops->free_tx_buffer(device, &descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}

	serialize_le16(data, auth_data->auth_algorithm);
	serialize_le16(data + 2, auth_data->auth_transaction);
	serialize_le16(data + 4, auth_data->status);

	u80211_header_description_t header = {
		.frame_control = U80211_HEADER_FRAME_CONTROL_SUBTYPE_AUTHENTICATION << 4,
		.addresses = {
			auth_data->address,
			device->metadata.mac_address,
			auth_data->address,
		},
	};

	status = u80211_serialize_header(&header, &descriptor);
	if (status != U80211_STATUS_SUCCESS) {
		device->ops->free_tx_buffer(device, &descriptor);
		return status;
	}

	return device->ops->transmit(device, &descriptor);
}

int u80211_send_probe_request(u80211_device_t *device) {
	size_t rate_count = 0;
	for (size_t rate = 0; rate <= MAX_RATE_VALUE; ++rate) {
		if (device->metadata.rate_bitmap[rate / 8] & (1 << (rate % 8)))
			++rate_count;
	}

	size_t supported_rate_count = min(rate_count, 8);
	size_t extended_rate_count = rate_count - supported_rate_count;
	size_t probe_request_data_size = 2 + 2 + supported_rate_count;
	if (extended_rate_count != 0)
		probe_request_data_size += 2 + extended_rate_count;

	u80211_tx_buffer_descriptor_t descriptor;
	int status = device->ops->allocate_tx_buffer(device, MANAGEMENT_HEADER_SIZE + probe_request_data_size, &descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	uint8_t *probe_request_data = u80211_descriptor_allocate_space(&descriptor, probe_request_data_size);
	if (probe_request_data == NULL) {
		device->ops->free_tx_buffer(device, &descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}

	probe_request_data[0] = IE_ID_SSID;
	probe_request_data[1] = 0;
	probe_request_data[2] = IE_ID_RATES;
	probe_request_data[3] = supported_rate_count;

	size_t rate_index = 0;
	for (size_t rate = 0; rate <= MAX_RATE_VALUE; ++rate) {
		if (!(device->metadata.rate_bitmap[rate / 8] & (1 << (rate % 8))))
			continue;

		if (rate_index == supported_rate_count) {
			probe_request_data[4 + rate_index] = IE_ID_RATES_EXT;
			probe_request_data[5 + rate_index] = extended_rate_count;
		}

		size_t offset = 4 + rate_index;
		if (rate_index >= supported_rate_count)
			offset += 2;
		probe_request_data[offset] = rate;
		++rate_index;
	}

	u80211_header_description_t header = {
		.frame_control = U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_REQUEST << 4,
		.addresses = {
			{ .bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
			device->metadata.mac_address,
			{ .bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
		},
	};

	status = u80211_serialize_header(&header, &descriptor);
	if (status != U80211_STATUS_SUCCESS) {
		device->ops->free_tx_buffer(device, &descriptor);
		return status;
	}

	return device->ops->transmit(device, &descriptor);
}
