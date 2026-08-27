#ifndef U80211_KERNEL_INTERFACE_H
#define U80211_KERNEL_INTERFACE_H

#include <stddef.h>

void *u80211_kernel_allocate(size_t size);
void u80211_kernel_free(void *ptr);

void *u80211_kernel_allocate_mutex(void);
void u80211_kernel_free_mutex(void *mutex);
void u80211_kernel_acquire_mutex(void *mutex);
void u80211_kernel_release_mutex(void *mutex);

void *u80211_kernel_allocate_semaphore(unsigned int initial_count);
void u80211_kernel_free_semaphore(void *semaphore);
void u80211_kernel_wait_semaphore(void *semaphore);
void u80211_kernel_signal_semaphore(void *semaphore);

void *u80211_kernel_allocate_spinlock(void);
void u80211_kernel_free_spinlock(void *spinlock);
void u80211_kernel_acquire_spinlock(void *spinlock);
void u80211_kernel_release_spinlock(void *spinlock);

void *u80211_kernel_allocate_rwlock(void);
void u80211_kernel_free_rwlock(void *rwlock);
void u80211_kernel_acquire_rwlock_exclusive(void *rwlock);
void u80211_kernel_acquire_rwlock_shared(void *rwlock);
void u80211_kernel_release_rwlock_exclusive(void *rwlock);
void u80211_kernel_release_rwlock_shared(void *rwlock);

typedef void (*u80211_kernel_work_fn_t)(void *context);

void *u80211_kernel_allocate_work(void);
// ms == 0 enqueues the work immediatelly. If work is already pending, the new request must be ignored.
void u80211_kernel_enqueue_work(void *work, u80211_kernel_work_fn_t function, void *context, size_t ms);
void u80211_kernel_free_work(void *work);

// buffer has an ethernet header.
void u80211_kernel_receive_callback(void *buffer);

#endif
