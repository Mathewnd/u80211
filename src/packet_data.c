#include <u80211/packet.h>
#include <u80211/string.h>
#include <u80211/kernel_interface.h>
#include <u80211/status.h>

#define ETHERNET_HEADER_SIZE 14
#define ETHERNET_MAX_FRAME_SIZE 1514
#define LLCSNAP_SIZE 8
#define TX_BUFFER_HEADROOM 64

// this is the expected LLC/SNAP header for our use-case. it is then followed by a big-endian 16-bit ethertype.
const uint8_t byte_header[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

void u80211_process_data_packet(u80211_device_t *device, u80211_header_description_t *header, void *data, size_t data_size) {
	if (data_size < LLCSNAP_SIZE)
		return;

	if (u80211_memcmp(data, byte_header, sizeof(byte_header)) != 0)
		return;

	uint16_t ethertype;
	u80211_memcpy(&ethertype, (void *)((uintptr_t)data + 6), sizeof(ethertype));

	// create an ethernet header before the payload
	// doing this in-place is safe due to the LLC/SNAP header + 802.11 header being much 
	// larger than the ethernet header.
	data = (void *)((uintptr_t)data + LLCSNAP_SIZE - ETHERNET_HEADER_SIZE);
	data_size = data_size + ETHERNET_HEADER_SIZE - LLCSNAP_SIZE;

	u80211_mac_address_t *destination;
	u80211_mac_address_t *source;
	bool to_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS;
	bool from_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS;

	if (to_ds) {
		destination = &header->addresses[2];
		source = from_ds ? &header->addresses[3] : &header->addresses[1];
	} else {
		destination = &header->addresses[0];
		source = from_ds ? &header->addresses[2] : &header->addresses[1];
	}

	u80211_memcpy(data, destination, sizeof(*destination));
	u80211_memcpy((void *)((uintptr_t)data + 6), source, sizeof(*source));
	u80211_memcpy((void *)((uintptr_t)data + 12), &ethertype, sizeof(ethertype));

	u80211_kernel_receive_callback(device, data, data_size);
}

int u80211_allocate_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor) {
	int status = device->ops->allocate_tx_buffer(device, ETHERNET_MAX_FRAME_SIZE + TX_BUFFER_HEADROOM, descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	descriptor->data = (void *)((uintptr_t)descriptor->data + TX_BUFFER_HEADROOM);
	descriptor->size -= TX_BUFFER_HEADROOM;
	descriptor->current_offset -= TX_BUFFER_HEADROOM;
	return U80211_STATUS_SUCCESS;
}

int u80211_transmit_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor) {
	descriptor->data = (uint8_t *)descriptor->data - TX_BUFFER_HEADROOM;
	descriptor->size += TX_BUFFER_HEADROOM;
	descriptor->current_offset += TX_BUFFER_HEADROOM;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	u80211_ap_t *ap = device->ap;
	if (u80211_get_device_state(device) != U80211_DEVICE_STATE_ASSOCIATED || ap == NULL) {
		u80211_kernel_release_spinlock(device->association_spinlock);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ASSOCIATED;
	}
	u80211_ap_hold(ap);
	u80211_kernel_release_spinlock(device->association_spinlock);

	size_t ethernet_frame_size = descriptor->size - descriptor->current_offset;
	if (ethernet_frame_size < ETHERNET_HEADER_SIZE) {
		u80211_ap_release(ap);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}

	uint8_t *ethernet_header = (uint8_t *)descriptor->data + descriptor->current_offset;
	u80211_mac_address_t destination;
	u80211_mac_address_t source;
	uint16_t ethertype;
	u80211_memcpy(&destination, ethernet_header, sizeof(destination));
	u80211_memcpy(&source, ethernet_header + 6, sizeof(source));
	u80211_memcpy(&ethertype, ethernet_header + 12, sizeof(ethertype));

	descriptor->current_offset += ETHERNET_HEADER_SIZE;
	uint8_t *llc_snap = u80211_descriptor_allocate_space(descriptor, LLCSNAP_SIZE);
	if (llc_snap == NULL) {
		u80211_ap_release(ap);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}
	u80211_memcpy(llc_snap, byte_header, sizeof(byte_header));
	u80211_memcpy(llc_snap + sizeof(byte_header), &ethertype, sizeof(ethertype));

	u80211_header_description_t header = {
		.frame_control = (U80211_HEADER_FRAME_CONTROL_TYPE_DATA << 2) | U80211_HEADER_FRAME_CONTROL_TO_DS,
		.sequence_control = __atomic_fetch_add(&device->tx_sequence_control, 0x10, __ATOMIC_RELAXED),
		.addresses = {
			ap->mac_address,
			source,
			destination,
		},
	};
	u80211_ap_release(ap);

	int status = u80211_serialize_header(&header, descriptor);
	if (status != U80211_STATUS_SUCCESS) {
		device->ops->free_tx_buffer(device, descriptor);
		return status;
	}

	const u80211_transmit_options_t options = { .key = u80211_select_key(device, &header) };
	return device->ops->transmit(device, descriptor, &options);
}
