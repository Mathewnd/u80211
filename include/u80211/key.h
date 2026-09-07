#ifndef U80211_KEY_H
#define U80211_KEY_H

#include <stddef.h>
#include <stdint.h>
#include <u80211/packet.h>

typedef enum {
	U80211_CIPHER_CCMP,
	U80211_CIPHER_TKIP,
	U80211_CIPHER_WEP40,
	U80211_CIPHER_WEP104,
} u80211_cipher_t;

typedef enum {
	U80211_KEY_PAIRWISE = 1 << 0,
	U80211_KEY_GROUP = 1 << 1,
	U80211_KEY_RX = 1 << 2,
	U80211_KEY_TX = 1 << 3,
} u80211_key_flags_t;

typedef struct {
	u80211_cipher_t cipher;
	uint8_t index;
	u80211_mac_address_t peer;
	const uint8_t *key;
	size_t key_len;
	const uint8_t *rx_seq;
	size_t rx_seq_len;
	uint32_t flags;
} u80211_key_t;

int u80211_set_key(u80211_device_t *device, const u80211_key_t *key);
int u80211_del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags);
int u80211_select_key(u80211_device_t *device, const u80211_header_description_t *header);

#endif
