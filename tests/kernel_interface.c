#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <u80211/kernel_interface.h>

typedef struct {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int stopping;
	int pending;
	struct timespec deadline;
	u80211_kernel_work_fn_t function;
	void *context;
} kernel_work_t;

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

	pthread_mutex_unlock(&work->mutex);
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
	if (work == NULL)
		return;

	pthread_mutex_lock(&work->mutex);
	work->stopping = 1;
	pthread_cond_signal(&work->condition);
	pthread_mutex_unlock(&work->mutex);

	pthread_join(work->thread, NULL);
	pthread_cond_destroy(&work->condition);
	pthread_mutex_destroy(&work->mutex);
	free(work);
}

void u80211_kernel_receive_callback(void *buffer) {
	(void)buffer;
}
