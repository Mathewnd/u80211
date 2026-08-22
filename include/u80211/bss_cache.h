#ifndef U80211_BSS_CACHE_H
#define U80211_BSS_CACHE_H

#include <u80211/ap.h>

int u80211_bss_cache_init(void);
void u80211_bss_cache_purge(void);
void u80211_bss_cache_insert(u80211_ap_t *ap);
void u80211_bss_cache_remove(u80211_mac_address_t *mac);
u80211_ap_t *u80211_bss_cache_find(u80211_mac_address_t *mac);

#endif
