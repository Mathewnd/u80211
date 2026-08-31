#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <time.h>
#include <u80211/kernel_interface.h>

#include "kernel_interface.h"

typedef struct {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int stopping;
	int pending;
	int destroy_on_exit;
	struct timespec deadline;
	u80211_kernel_work_fn_t function;
	void *context;
} kernel_work_t;

static pthread_mutex_t receive_handler_mutex = PTHREAD_MUTEX_INITIALIZER;
static receive_handler_t receive_handler;
static void *receive_handler_context;

void set_receive_handler(receive_handler_t handler, void *context) {
	pthread_mutex_lock(&receive_handler_mutex);
	receive_handler = handler;
	receive_handler_context = context;
	pthread_mutex_unlock(&receive_handler_mutex);
}

static void destroy_work(kernel_work_t *work) {
	pthread_cond_destroy(&work->condition);
	pthread_mutex_destroy(&work->mutex);
	free(work);
}

static int timespec_compare(const struct timespec *a, const struct timespec *b) {
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	return 0;
}

static struct timespec deadline_after_ms(size_t ms) {
	struct timespec deadline;
	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += (time_t)(ms / 1000);
	deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	return deadline;
}

static void *work_thread(void *argument) {
	kernel_work_t *work = argument;
	pthread_mutex_lock(&work->mutex);

	for (;;) {
		while (!work->stopping && !work->pending)
			pthread_cond_wait(&work->condition, &work->mutex);
		if (work->stopping)
			break;

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		while (!work->stopping && timespec_compare(&now, &work->deadline) < 0) {
			int error = pthread_cond_timedwait(&work->condition, &work->mutex, &work->deadline);
			if (error != 0 && error != ETIMEDOUT)
				continue;

			clock_gettime(CLOCK_MONOTONIC, &now);
		}

		if (work->stopping)
			break;

		u80211_kernel_work_fn_t function = work->function;
		void *context = work->context;
		work->pending = 0;

		pthread_mutex_unlock(&work->mutex);
		function(context);
		pthread_mutex_lock(&work->mutex);
	}

	int destroy_on_exit = work->destroy_on_exit;
	pthread_mutex_unlock(&work->mutex);
	if (destroy_on_exit)
		destroy_work(work);
	return NULL;
}

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
	pthread_mutex_destroy(mutex);
	free(mutex);
}

void u80211_kernel_acquire_mutex(void *mutex) {
	pthread_mutex_lock(mutex);
}

void u80211_kernel_release_mutex(void *mutex) {
	pthread_mutex_unlock(mutex);
}

void *u80211_kernel_allocate_semaphore(unsigned int initial_count) {
	sem_t *semaphore = malloc(sizeof(*semaphore));
	if (semaphore == NULL)
		return NULL;

	if (sem_init(semaphore, 0, initial_count) != 0) {
		free(semaphore);
		return NULL;
	}

	return semaphore;
}

void u80211_kernel_free_semaphore(void *semaphore) {
	sem_destroy(semaphore);
	free(semaphore);
}

void u80211_kernel_wait_semaphore(void *semaphore) {
	for (;;) {
		if (sem_wait(semaphore) == 0 || errno != EINTR)
			return;
	}
}

void u80211_kernel_signal_semaphore(void *semaphore) {
	sem_post(semaphore);
}

void *u80211_kernel_allocate_spinlock(void) {
	void *spinlock = malloc(sizeof(pthread_spinlock_t));
	if (spinlock == NULL)
		return NULL;

	if (pthread_spin_init(spinlock, PTHREAD_PROCESS_PRIVATE) != 0) {
		free(spinlock);
		return NULL;
	}

	return spinlock;
}

void u80211_kernel_free_spinlock(void *spinlock) {
	pthread_spin_destroy(spinlock);
	free(spinlock);
}

void u80211_kernel_acquire_spinlock(void *spinlock) {
	pthread_spin_lock(spinlock);
}

void u80211_kernel_release_spinlock(void *spinlock) {
	pthread_spin_unlock(spinlock);
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

void *u80211_kernel_allocate_work(void) {
	kernel_work_t *work = calloc(1, sizeof(*work));
	if (work == NULL)
		return NULL;

	if (pthread_mutex_init(&work->mutex, NULL) != 0) {
		free(work);
		return NULL;
	}

	pthread_condattr_t attributes;
	if (pthread_condattr_init(&attributes) != 0) {
		pthread_mutex_destroy(&work->mutex);
		free(work);
		return NULL;
	}

	if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) != 0 || pthread_cond_init(&work->condition, &attributes) != 0) {
		pthread_condattr_destroy(&attributes);
		pthread_mutex_destroy(&work->mutex);
		free(work);
		return NULL;
	}
	pthread_condattr_destroy(&attributes);

	if (pthread_create(&work->thread, NULL, work_thread, work) != 0) {
		pthread_cond_destroy(&work->condition);
		pthread_mutex_destroy(&work->mutex);
		free(work);
		return NULL;
	}

	return work;
}

void u80211_kernel_enqueue_work(void *opaque_work, u80211_kernel_work_fn_t function, void *context, size_t ms) {
	kernel_work_t *work = opaque_work;
	pthread_mutex_lock(&work->mutex);
	if (!work->pending) {
		work->function = function;
		work->context = context;
		work->deadline = deadline_after_ms(ms);
		work->pending = 1;
		pthread_cond_signal(&work->condition);
	}
	pthread_mutex_unlock(&work->mutex);
}

void u80211_kernel_free_work(void *opaque_work) {
	kernel_work_t *work = opaque_work;
	int destroy_on_exit = pthread_equal(pthread_self(), work->thread);

	pthread_mutex_lock(&work->mutex);
	work->stopping = 1;
	work->destroy_on_exit = destroy_on_exit;
	pthread_cond_signal(&work->condition);
	pthread_mutex_unlock(&work->mutex);

	if (destroy_on_exit) {
		pthread_detach(work->thread);
		return;
	}

	pthread_join(work->thread, NULL);
	destroy_work(work);
}

void u80211_kernel_receive_callback(u80211_device_t *device, void *buffer, size_t size) {
	pthread_mutex_lock(&receive_handler_mutex);
	receive_handler_t handler = receive_handler;
	void *context = receive_handler_context;
	pthread_mutex_unlock(&receive_handler_mutex);

	if (handler != NULL)
		handler(device, buffer, size, context);
}
