#include <u80211/string.h>
#include <stdint.h>

void *u80211_memcpy(void *destination, const void *source, size_t size) {
	uint8_t *destination_bytes = destination;
	const uint8_t *source_bytes = source;

	for (size_t i = 0; i < size; i++)
		destination_bytes[i] = source_bytes[i];

	return destination;
}

void *u80211_memset(void *destination, int value, size_t size) {
	uint8_t *destination_bytes = destination;

	for (size_t i = 0; i < size; i++)
		destination_bytes[i] = (uint8_t)value;

	return destination;
}
