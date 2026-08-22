#ifndef U80211_UTIL_H
#define U80211_UTIL_H

#include <stdint.h>

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define container_of(ptr, type, member) \
	((type *)((uintptr_t)ptr - offsetof(type, member)))

#endif
