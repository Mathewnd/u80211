#include <u80211/status.h>
#include <u80211/kernel_interface.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

typedef struct {
	u80211_list_node_t node;
	uint8_t index;
	u80211_mac_address_t peer;
	uint32_t flags;
} u80211_key_metadata_t;

static bool key_identity_equal(const u80211_key_metadata_t *metadata, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	return metadata->index == index && metadata->flags == flags && u80211_mac_address_equal(&metadata->peer, peer);
}

int u80211_key_state_init(u80211_device_t *device) {
	u80211_list_init(&device->keys);
	device->key_spinlock = u80211_kernel_allocate_spinlock();
	if (device->key_spinlock == NULL)
		return U80211_STATUS_ENOMEM;

	return U80211_STATUS_SUCCESS;
}

void u80211_key_state_deinit(u80211_device_t *device) {
	u80211_list_node_t *node;
	while ((node = u80211_list_pop_front(&device->keys)) != NULL)
		u80211_kernel_free(container_of(node, u80211_key_metadata_t, node));

	u80211_kernel_free_spinlock(device->key_spinlock);
	device->key_spinlock = NULL;
}

int u80211_set_key(u80211_device_t *device, const u80211_key_t *key) {
	if (device->ops->set_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	u80211_key_metadata_t *metadata = u80211_kernel_allocate(sizeof(*metadata));
	if (metadata == NULL)
		return U80211_STATUS_ENOMEM;

	metadata->index = key->index;
	metadata->peer = key->peer;
	metadata->flags = key->flags;

	int status = device->ops->set_key(device, key);
	if (status != U80211_STATUS_SUCCESS) {
		u80211_kernel_free(metadata);
		return status;
	}

	u80211_key_metadata_t *old_metadata = NULL;
	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *candidate = container_of(node, u80211_key_metadata_t, node);
		if (key_identity_equal(candidate, key->index, &key->peer, key->flags)) {
			old_metadata = candidate;
			u80211_list_remove(&device->keys, node);
			break;
		}
	}
	u80211_list_push_front(&device->keys, &metadata->node);
	u80211_kernel_release_spinlock(device->key_spinlock);

	if (old_metadata != NULL)
		u80211_kernel_free(old_metadata);
	return U80211_STATUS_SUCCESS;
}

int u80211_del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	if (device->ops->del_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	int status = device->ops->del_key(device, index, peer, flags);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	u80211_key_metadata_t *removed_metadata = NULL;
	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *candidate = container_of(node, u80211_key_metadata_t, node);
		if (key_identity_equal(candidate, index, peer, flags)) {
			removed_metadata = candidate;
			u80211_list_remove(&device->keys, node);
			break;
		}
	}
	u80211_kernel_release_spinlock(device->key_spinlock);

	if (removed_metadata != NULL)
		u80211_kernel_free(removed_metadata);
	return U80211_STATUS_SUCCESS;
}

int u80211_select_key(u80211_device_t *device, const u80211_header_description_t *header) {
	bool tx = u80211_mac_address_equal(&header->addresses[1], &device->metadata.mac_address);
	bool group = header->addresses[0].bytes[0] & 1;
	uint32_t direction_flag = tx ? U80211_KEY_TX : U80211_KEY_RX;
	uint32_t type_flag = group ? U80211_KEY_GROUP : U80211_KEY_PAIRWISE;
	const u80211_mac_address_t *peer = tx ? &header->addresses[0] : &header->addresses[1];
	int index = -1;

	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *metadata = container_of(node, u80211_key_metadata_t, node);
		if (!(metadata->flags & direction_flag) || !(metadata->flags & type_flag))
			continue;
		if (!group && !u80211_mac_address_equal(&metadata->peer, peer))
			continue;

		index = metadata->index;
		break;
	}
	u80211_kernel_release_spinlock(device->key_spinlock);
	return index;
}
