#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <u80211/kernel_interface.h>

void *u80211_kernel_allocate(size_t size) {
	return malloc(size);
}

void u80211_kernel_free(void *ptr) {
	free(ptr);
}

void *u80211_kernel_allocate_mutex(void) {
	pthread_mutex_t *mutex = malloc(sizeof(*mutex));
	if (mutex == NULL)
		return NULL;

	if (pthread_mutex_init(mutex, NULL) != 0) {
		free(mutex);
		return NULL;
	}

	return mutex;
}

void u80211_kernel_free_mutex(void *mutex) {
	if (mutex == NULL)
		return;

	pthread_mutex_destroy(mutex);
	free(mutex);
}

void u80211_kernel_acquire_mutex(void *mutex) {
	pthread_mutex_lock(mutex);
}

void u80211_kernel_release_mutex(void *mutex) {
	pthread_mutex_unlock(mutex);
}

void *u80211_kernel_allocate_rwlock(void) {
	pthread_rwlock_t *rwlock = malloc(sizeof(*rwlock));
	if (rwlock == NULL)
		return NULL;

	if (pthread_rwlock_init(rwlock, NULL) != 0) {
		free(rwlock);
		return NULL;
	}

	return rwlock;
}

void u80211_kernel_free_rwlock(void *rwlock) {
	if (rwlock == NULL)
		return;

	pthread_rwlock_destroy(rwlock);
	free(rwlock);
}

void u80211_kernel_acquire_rwlock_exclusive(void *rwlock) {
	pthread_rwlock_wrlock(rwlock);
}

void u80211_kernel_acquire_rwlock_shared(void *rwlock) {
	pthread_rwlock_rdlock(rwlock);
}

void u80211_kernel_release_rwlock_exclusive(void *rwlock) {
	pthread_rwlock_unlock(rwlock);
}

void u80211_kernel_release_rwlock_shared(void *rwlock) {
	pthread_rwlock_unlock(rwlock);
}

void u80211_kernel_receive_callback(void *buffer) {
	(void)buffer;
}
