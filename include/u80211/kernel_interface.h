#ifndef U80211_KERNEL_INTERFACE_H
#define U80211_KERNEL_INTERFACE_H

#include <stddef.h>

void *u80211_kernel_allocate(size_t size);
void u80211_kernel_free(void *ptr);

void *u80211_kernel_allocate_mutex(void);
void u80211_kernel_free_mutex(void *mutex);
void u80211_kernel_acquire_mutex(void *mutex);
void u80211_kernel_release_mutex(void *mutex);

void *u80211_kernel_allocate_rwlock(void);
void u80211_kernel_free_rwlock(void *rwlock);
void u80211_kernel_acquire_rwlock_exclusive(void *rwlock);
void u80211_kernel_acquire_rwlock_shared(void *rwlock);
void u80211_kernel_release_rwlock_exclusive(void *rwlock);
void u80211_kernel_release_rwlock_shared(void *rwlock);

// buffer has an ethernet header.
void u80211_kernel_receive_callback(void *buffer);

#endif
