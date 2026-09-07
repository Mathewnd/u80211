#include <u80211/status.h>
#include <u80211/u80211.h>

int u80211_set_key(u80211_device_t *device, const u80211_key_t *key) {
	if (device->ops->set_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	return device->ops->set_key(device, key);
}

int u80211_del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	if (device->ops->del_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	return device->ops->del_key(device, index, peer, flags);
}
