#include <stddef.h>

#include <u80211/bss_cache.h>
#include <u80211/kernel_interface.h>
#include <u80211/rbtree.h>
#include <u80211/status.h>
#include <u80211/util.h>

static void *rwlock;
static u80211_rbtree_t *cache;

static int mac_compare(u80211_mac_address_t *a, u80211_mac_address_t *b) {
	for (size_t i = 0; i < sizeof(a->bytes); i++) {
		if (a->bytes[i] < b->bytes[i])
			return -1;
		if (a->bytes[i] > b->bytes[i])
			return 1;
	}

	return 0;
}

static int rbtree_mac_compare(u80211_rbtree_t *a, u80211_rbtree_t *b) {
	u80211_ap_t *ap_a = container_of(a, u80211_ap_t, cache_node);
	u80211_ap_t *ap_b = container_of(b, u80211_ap_t, cache_node);

	return mac_compare(&ap_a->mac_address, &ap_b->mac_address);
}

static int rbtree_value_compare(void *a, u80211_rbtree_t *b) {
	u80211_ap_t *ap_b = container_of(b, u80211_ap_t, cache_node);

	return mac_compare(a, &ap_b->mac_address);
}

int u80211_bss_cache_init(void) {
	rwlock = u80211_kernel_allocate_rwlock();
	if (unlikely(rwlock == NULL))
		return U80211_STATUS_ENOMEM;

	return 0;
}


void u80211_bss_cache_purge(void) {
	u80211_kernel_acquire_rwlock_exclusive(rwlock);

	u80211_rbtree_t *iterator = u80211_rbtree_first(cache);
	while (iterator) {
		u80211_rbtree_t *next = u80211_rbtree_successor(iterator);

		u80211_ap_t *ap = container_of(iterator, u80211_ap_t, cache_node); 
		if (__atomic_load_n(&ap->refcount, __ATOMIC_RELAXED) == 1) {
			u80211_rbtree_remove(&cache, iterator);
			u80211_ap_release(ap);
		}

		iterator = next;
	}

	u80211_kernel_release_rwlock_exclusive(rwlock);
}

void u80211_bss_cache_insert(u80211_ap_t *ap) {
	u80211_kernel_acquire_rwlock_exclusive(rwlock);

	// TODO check if already on cache
	u80211_rbtree_insert(&cache, &ap->cache_node, rbtree_mac_compare);
	u80211_ap_hold(ap);

	u80211_kernel_release_rwlock_exclusive(rwlock);
}

void u80211_bss_cache_remove(u80211_mac_address_t *mac) {
	u80211_kernel_acquire_rwlock_exclusive(rwlock);

	u80211_rbtree_t *node = u80211_rbtree_lookup(cache, mac, rbtree_value_compare);
	u80211_rbtree_remove(&cache, node);

	u80211_ap_t *ap = container_of(node, u80211_ap_t, cache_node); 
	u80211_ap_release(ap);
	
	u80211_kernel_release_rwlock_exclusive(rwlock);
}

u80211_ap_t *u80211_bss_cache_find(u80211_mac_address_t *mac) {
	u80211_kernel_acquire_rwlock_shared(rwlock);

	u80211_rbtree_t *node = u80211_rbtree_lookup(cache, mac, rbtree_value_compare);

	u80211_ap_t *ap = container_of(node, u80211_ap_t, cache_node); 
	u80211_ap_hold(ap);
	
	u80211_kernel_release_rwlock_shared(rwlock);

	return ap;
}
