#include <u80211/u80211.h>
#include <u80211/kernel_interface.h>
#include <u80211/status.h>

#define AUTH_TIMEOUT 5000

typedef struct {
	void *completion_work;
	void *auth_timeout_work;
	unsigned int generation;
	unsigned int timeouts_in_flight;
	u80211_device_t *device;
	u80211_ap_t *ap; // this keeps a reference, the reference in the device is only incremented only when fully associated
} u80211_association_context_t;

static void destroy_association_context(u80211_association_context_t *association_context) {
	u80211_kernel_free_work(association_context->completion_work);
	u80211_kernel_free_work(association_context->auth_timeout_work);
	u80211_kernel_free(association_context);
}

void u80211_association_process_response(u80211_device_t *device, u80211_association_response_data_t *association_data) {
	(void)device;
	(void)association_data;
	// TODO: handle the association response
}

static void auth_completion_work(void *ctx) {
	u80211_association_context_t *association_context = ctx;
	u80211_device_t *device = association_context->device;

	// TODO rest of the owl
}

void u80211_association_process_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data) {
	u80211_kernel_acquire_spinlock(device->association_spinlock);

	// not associating/different AP
	if (!device->ap || !u80211_mac_address_equal(&device->ap->mac_address, &auth_data->address))
		goto leave;

	// not the same algo/different transaction stage than expected
	if (auth_data->auth_algorithm != U80211_AUTH_ALGORITHM_OPEN || auth_data->auth_transaction != 2)
		goto leave;

	if (auth_data->status) {
		// failure
		if (!u80211_set_device_state(device, U80211_DEVICE_STATE_AUTHENTICATING, U80211_DEVICE_STATE_DOWN))
			goto leave;

		// TODO: wake up waiters with error
		device->ap = NULL;
		device->association_context = NULL;
		++device->association_generation;
	} else {
		if (!u80211_set_device_state(device, U80211_DEVICE_STATE_AUTHENTICATING, U80211_DEVICE_STATE_ASSOCIATING))
			goto leave;

		u80211_association_context_t *ctx = device->association_context;
		u80211_kernel_enqueue_work(ctx->completion_work, auth_completion_work, ctx, 0);
	}

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
}

static void auth_timeout(void *ctx) {
	u80211_association_context_t *association_context = ctx;
	u80211_device_t *device = association_context->device;
	bool free = true;

	u80211_kernel_acquire_spinlock(device->association_spinlock);

	// not associating/different association generation
	u80211_ap_t *ap_release = NULL;
	if (!device->ap || device->association_generation != association_context->generation)
		goto leave;

	if (u80211_set_device_state(device, U80211_DEVICE_STATE_AUTHENTICATING, U80211_DEVICE_STATE_DOWN)) {
		device->ap = NULL;
		++device->association_generation;
		// TODO: wake up waiters
	} else {
		// already progressed
		free = false;
	}

leave:
	u80211_kernel_release_spinlock(device->association_spinlock);
	if (__atomic_sub_fetch(&association_context->timeouts_in_flight, 1, __ATOMIC_ACQ_REL) == 0 && free) {
		u80211_ap_release(association_context->ap);
		destroy_association_context(association_context);
	}
}

int u80211_associate(u80211_device_t *device, u80211_ap_t *ap) {
	u80211_association_context_t *association_context = u80211_kernel_allocate(sizeof(u80211_association_context_t));
	if (association_context == NULL)
		return U80211_STATUS_ENOMEM;

	association_context->completion_work = u80211_kernel_allocate_work();
	if (association_context->completion_work == NULL) {
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->auth_timeout_work = u80211_kernel_allocate_work();
	if (association_context->auth_timeout_work == NULL) {
		u80211_kernel_free_work(association_context->completion_work);
		u80211_kernel_free(association_context);
		return U80211_STATUS_ENOMEM;
	}

	association_context->device = device;
	association_context->ap = ap;
	__atomic_store_n(&association_context->timeouts_in_flight, 1, __ATOMIC_RELAXED);

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	if (!u80211_set_device_state(device, U80211_DEVICE_STATE_DOWN, U80211_DEVICE_STATE_AUTHENTICATING)) {
		u80211_kernel_release_spinlock(device->association_spinlock);
		destroy_association_context(association_context);
		return U80211_STATUS_BUSY;
	}

	association_context->generation = device->association_generation;
	device->association_context = association_context;
	device->ap = ap;
	u80211_ap_hold(ap);
	u80211_kernel_release_spinlock(device->association_spinlock);

	u80211_auth_data_t auth_data = {
		.address = ap->mac_address,
		.auth_algorithm = U80211_AUTH_ALGORITHM_OPEN,
		.auth_transaction = 1,
		.status = 0,
	};
	device->ops->set_channel(device, ap->channel);
	u80211_send_authentication(device, &auth_data);

	u80211_kernel_enqueue_work(association_context->auth_timeout_work, auth_timeout, association_context, AUTH_TIMEOUT);

	return 0;
}
