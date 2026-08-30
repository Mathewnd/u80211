#include <u80211/packet.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

void u80211_process_packet(u80211_device_t *device, void *packet, size_t packet_size) {
	__atomic_add_fetch(&device->packet_count, 1, __ATOMIC_RELAXED);

	void *data_start;
	size_t data_size;
	u80211_header_description_t header;
	if (u80211_deserialize_header(packet, packet_size, &header, &data_start, &data_size) != U80211_STATUS_SUCCESS)
		return;

	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header.frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			u80211_process_management_packet(device, &header, data_start, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			u80211_process_data_packet(device, &header, data_start, data_size);
			break;
	}
}

size_t u80211_get_packet_count(u80211_device_t *device) {
	return __atomic_load_n(&device->packet_count, __ATOMIC_RELAXED);
}

void u80211_reset_packet_count(u80211_device_t *device) {
	__atomic_store_n(&device->packet_count, 0, __ATOMIC_RELAXED);
}

static int deserialize_management_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	if (source_size < 14)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (const void *)((uintptr_t)source + 6), 6);
	header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 12));

	*data_start = (void *)((uintptr_t)source + 14);
	*data_size = source_size - 14;
	return U80211_STATUS_SUCCESS;
}

static int deserialize_control_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	(void)source;
	(void)source_size;
	(void)header;
	(void)data_start;
	(void)data_size;
	return U80211_STATUS_UNSUPPORTED;
}

static int deserialize_data_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	size_t header_size = (header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS) ? 20 : 14;
	if (source_size < header_size)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (const void *)((uintptr_t)source + 6), 6);

	if (header_size == 20) {
		u80211_memcpy(&header->addresses[3], (const void *)((uintptr_t)source + 12), 6);
		header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 18));
	} else {
		header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 12));
	}

	*data_start = (void *)((uintptr_t)source + header_size);
	*data_size = source_size - header_size;
	return U80211_STATUS_SUCCESS;
}

int u80211_deserialize_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	if (source_size < 10)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	header->frame_control = deserialize_le16(source);
	header->duration_id = deserialize_le16((const void *)((uintptr_t)source + 2));
	u80211_memcpy(&header->addresses[0], (const void *)((uintptr_t)source + 4), 6);

	void *next_part = (void *)((uintptr_t)source + 10);
	size_t next_part_size = source_size - 10;

	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return deserialize_management_header(next_part, next_part_size, header, data_start, data_size);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return deserialize_control_header(next_part, next_part_size, header, data_start, data_size);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return deserialize_data_header(next_part, next_part_size, header, data_start, data_size);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}


static void serialize_common_header(u80211_header_description_t *header, uint8_t *destination) {
	serialize_le16(destination, header->frame_control);
	serialize_le16(destination + 2, header->duration_id);
	u80211_memcpy(destination + 4, &header->addresses[0], 6);
}

static int serialize_management_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	uint8_t *destination = u80211_descriptor_allocate_space(descriptor, 24);
	if (destination == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	serialize_common_header(header, destination);
	u80211_memcpy(destination + 10, &header->addresses[1], 6);
	u80211_memcpy(destination + 16, &header->addresses[2], 6);
	serialize_le16(destination + 22, header->sequence_control);

	return U80211_STATUS_SUCCESS;
}

static int serialize_control_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	(void)header;
	(void)descriptor;
	return U80211_STATUS_UNSUPPORTED;
}

static int serialize_data_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	size_t header_size = (header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS) ? 30 : 24;

	uint8_t *destination = u80211_descriptor_allocate_space(descriptor, header_size);
	if (destination == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

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

int u80211_serialize_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return serialize_management_header(header, descriptor);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return serialize_control_header(header, descriptor);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return serialize_data_header(header, descriptor);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}
