#include <u80211/ap.h>
#include <u80211/kernel_interface.h>

void u80211_ap_inactive(u80211_ap_t *ap) {
	__atomic_thread_fence(__ATOMIC_ACQ_REL);
	u80211_kernel_free(ap);
}
