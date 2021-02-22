#ifndef CUCKOO_FILTER_H
#define CUCKOO_FILTER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CUCKOO_FILTER_VERSION_MAJOR 1
#define CUCKOO_FILTER_VERSION_MINOR 0
#define CUCKOO_FILTER_VERSION_PATCH 0
#define CUCKOO_FILTER_VERSION_ID                                               \
	CUCKOO_FILTER_VERSION_MAJOR * 10000 +                                  \
		CUCKOO_FILTER_VERSION_MINOR * 100 +                            \
		CUCKOO_FILTER_VERSION_PATCH

#define CUCKOO_FILTER_VERSION                                                  \
	TOSTRING(CUCKOO_FILTER_VERSION_MAJOR)                                  \
	"." TOSTRING(CUCKOO_FILTER_VERSION_MINOR) "." TOSTRING(                \
		CUCKOO_FILTER_VERSION_PATCH)

typedef enum {
	CUCKOO_FILTER_OK = 0,
	CUCKOO_FILTER_NOT_FOUND = 1,
	CUCKOO_FILTER_FULL = 2,
	CUCKOO_FILTER_RETRY = 3,
	CUCKOO_FILTER_ERROR = 4
} CUCKOO_FILTER_RETURN;

typedef struct cuckoo_filter_t cuckoo_filter_t;

typedef CUCKOO_FILTER_RETURN (*cuckoo_allocate)(cuckoo_filter_t **filter,
						size_t size);
typedef CUCKOO_FILTER_RETURN (*cuckoo_deallocate)(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed,
		  cuckoo_allocate allocator);

CUCKOO_FILTER_RETURN
cuckoo_filter_load(cuckoo_filter_t **filter, int fd, cuckoo_allocate allocator);

CUCKOO_FILTER_RETURN
cuckoo_filter_save(cuckoo_filter_t *filter, int fd);

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const void *key, size_t keylen);

// Allocators
// SHM
CUCKOO_FILTER_RETURN cuckoo_filter_shm_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_shm_alloc(cuckoo_filter_t **filter,
					     size_t size);
// Single process
CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_alloc(cuckoo_filter_t **filter, size_t size);

#endif
