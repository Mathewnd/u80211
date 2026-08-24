#include <stdbool.h>

#include <u80211/packet.h>
#include <u80211/string.h>
#include <u80211/util.h>

#define IE_ID_SSID 0
#define IE_ID_RATES 1
#define IE_ID_CHANNEL 3
#define IE_ID_RATES_EXT 50

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

static uint16_t deserialize_le16(const void *source) {
	uint16_t value;
	u80211_memcpy(&value, source, sizeof(value));
	return le_to_host(value);
}

static void process_probe_response(u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < 12)
		return;

	u80211_beacon_data_t beacon_data;
	beacon_data.interval = deserialize_le16((const void *)((uintptr_t)data + 8));
	beacon_data.capabilities = deserialize_le16((const void *)((uintptr_t)data + 10));
	u80211_memset(&beacon_data.rate_bitmap, 0, sizeof(beacon_data.rate_bitmap));
	beacon_data.mac_address = header->addresses[2];
	beacon_data.ssid[0] = '\0';
	beacon_data.channel = 0;

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

	// TODO pass data to higher layer
}

void u80211_process_management_packet(u80211_header_description_t *header, const void *data, size_t data_size) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	switch (subtype) {
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_RESPONSE:
			process_probe_response(header, data, data_size);
	}
}
