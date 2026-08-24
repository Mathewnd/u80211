#include <u80211/packet.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

void u80211_process_packet(u80211_device_t *device, const void *packet, size_t packet_size) {
	__atomic_add_fetch(&device->packet_count, 1, __ATOMIC_RELAXED);
}

size_t u80211_get_packet_count(u80211_device_t *device) {
	return __atomic_load_n(&device->packet_count, __ATOMIC_RELAXED);
}

void u80211_reset_packet_count(u80211_device_t *device) {
	__atomic_store_n(&device->packet_count, 0, __ATOMIC_RELAXED);
}

static uint16_t deserialize_le16(void *source) {
	uint16_t value;
	u80211_memcpy(&value, source, sizeof(value));
	return le_to_host(value);
}

static void serialize_le16(uint8_t *destination, uint16_t value) {
	uint16_t little_endian_value = host_to_le(value);
	u80211_memcpy(destination, &little_endian_value, sizeof(little_endian_value));
}

static int deserialize_management_header(void *source, u80211_header_description_t *header, void **data_start) {
	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (void *)((uintptr_t)source + 6), 6);
	header->sequence_control = deserialize_le16((void *)((uintptr_t)source + 12));

	*data_start = (void *)((uintptr_t)source + 14);
	return U80211_STATUS_SUCCESS;
}

static int deserialize_control_header(void *source, u80211_header_description_t *header, void **data_start) {
	(void)source;
	(void)header;
	(void)data_start;
	return U80211_STATUS_UNSUPPORTED;
}

static int deserialize_data_header(void *source, u80211_header_description_t *header, void **data_start) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (void *)((uintptr_t)source + 6), 6);

	if ((header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS)) {
		u80211_memcpy(&header->addresses[3], (void *)((uintptr_t)source + 12), 6);
		header->sequence_control = deserialize_le16((void *)((uintptr_t)source + 18));
		*data_start = (void *)((uintptr_t)source + 20);
	} else {
		header->sequence_control = deserialize_le16((void *)((uintptr_t)source + 12));
		*data_start = (void *)((uintptr_t)source + 14);
	}

	return U80211_STATUS_SUCCESS;
}

int u80211_deserialize_header(void *source, u80211_header_description_t *header, void **data_start) {
	header->frame_control = deserialize_le16(source);
	header->duration_id = deserialize_le16((void *)((uintptr_t)source + 2));
	u80211_memcpy(&header->addresses[0], (void *)((uintptr_t)source + 4), 6);

	void *next_part = (void *)((uintptr_t)source + 10);

	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return deserialize_management_header(next_part, header, data_start);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return deserialize_control_header(next_part, header, data_start);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return deserialize_data_header(next_part, header, data_start);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}


static void serialize_common_header(u80211_header_description_t *header, uint8_t *destination) {
	serialize_le16(destination, header->frame_control);
	serialize_le16(destination + 2, header->duration_id);
	u80211_memcpy(destination + 4, &header->addresses[0], 6);
}

static int serialize_management_header(u80211_header_description_t *header, void *destination_end, size_t space_available) {
	if (space_available < 24)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	uint8_t *destination = (uint8_t *)destination_end - 24;

	serialize_common_header(header, destination);
	u80211_memcpy(destination + 10, &header->addresses[1], 6);
	u80211_memcpy(destination + 16, &header->addresses[2], 6);
	serialize_le16(destination + 22, header->sequence_control);

	return U80211_STATUS_SUCCESS;
}

static int serialize_control_header(u80211_header_description_t *header, void *destination_end, size_t space_available) {
	(void)header;
	(void)destination_end;
	(void)space_available;
	return U80211_STATUS_UNSUPPORTED;
}

static int serialize_data_header(u80211_header_description_t *header, void *destination_end, size_t space_available) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	size_t header_size = (header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS) ? 30 : 24;

	if (space_available < header_size)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	uint8_t *destination = (uint8_t *)destination_end - header_size;

	serialize_common_header(header, destination);
	u80211_memcpy(destination + 10, &header->addresses[1], 6);
	u80211_memcpy(destination + 16, &header->addresses[2], 6);

	if (header_size == 30) {
		u80211_memcpy(destination + 22, &header->addresses[3], 6);
		serialize_le16(destination + 28, header->sequence_control);
	} else {
		serialize_le16(destination + 22, header->sequence_control);
	}

	return U80211_STATUS_SUCCESS;
}

int u80211_serialize_header(u80211_header_description_t *header, void *destination_end, size_t space_available) {
	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return serialize_management_header(header, destination_end, space_available);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return serialize_control_header(header, destination_end, space_available);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return serialize_data_header(header, destination_end, space_available);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}
