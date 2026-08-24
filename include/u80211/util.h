#ifndef U80211_UTIL_H
#define U80211_UTIL_H

#include <stdint.h>

static inline uint16_t u80211_host_to_le16(uint16_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap16(value);
#endif
}

static inline uint32_t u80211_host_to_le32(uint32_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap32(value);
#endif
}

static inline uint64_t u80211_host_to_le64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap64(value);
#endif
}

static inline uint16_t u80211_le_to_host16(uint16_t value) {
	return u80211_host_to_le16(value);
}

static inline uint32_t u80211_le_to_host32(uint32_t value) {
	return u80211_host_to_le32(value);
}

static inline uint64_t u80211_le_to_host64(uint64_t value) {
	return u80211_host_to_le64(value);
}

#define host_to_le(value) _Generic((value), \
	uint16_t: u80211_host_to_le16, \
	uint32_t: u80211_host_to_le32, \
	uint64_t: u80211_host_to_le64 \
)(value)

#define le_to_host(value) _Generic((value), \
	uint16_t: u80211_le_to_host16, \
	uint32_t: u80211_le_to_host32, \
	uint64_t: u80211_le_to_host64 \
)(value)

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define min(a, b)	((a) < (b) ? (a) : (b))

#define container_of(ptr, type, member) \
	((type *)((uintptr_t)ptr - offsetof(type, member)))

#endif
