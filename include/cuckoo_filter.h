#ifndef CUCKOO_FILTER_H
#define CUCKOO_FILTER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef enum {
	CUCKOO_FILTER_OK = 0,
	CUCKOO_FILTER_NOT_FOUND,
	CUCKOO_FILTER_FULL,
	CUCKOO_FILTER_ALLOCATION_FAILED,
	CUCKOO_FILTER_BUSY,
	CUCKOO_FILTER_RETRY,
	CUCKOO_FILTER_SEMERR,
} CUCKOO_FILTER_RETURN;

typedef struct cuckoo_filter_t cuckoo_filter_t;

CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed);

CUCKOO_FILTER_RETURN
cuckoo_filter_shm_new(const char *name, cuckoo_filter_t **filter,
		      uint64_t max_key_count, size_t max_kick_attempts,
		      uint32_t seed);

CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const uint8_t *key,
		  uint64_t key_length_in_bytes);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const uint8_t *key,
		     uint64_t key_length_in_bytes);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const uint8_t *key,
		       uint64_t key_length_in_bytes);
const char *cuckoo_strerr(CUCKOO_FILTER_RETURN);
#endif /* CUCKOO_FILTER_H */

