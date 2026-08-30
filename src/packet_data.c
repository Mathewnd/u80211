#include <u80211/packet.h>
#include <u80211/string.h>
#include <u80211/kernel_interface.h>

#define ETHERNET_HEADER_SIZE 14
#define LLCSNAP_SIZE 8

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
	data_size = data_size + LLCSNAP_SIZE - ETHERNET_HEADER_SIZE;

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
