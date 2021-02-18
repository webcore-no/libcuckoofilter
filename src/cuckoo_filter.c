#include "../include/cuckoo_filter.h"
#define XXH_STATIC_LINKING_ONLY /* access advanced declarations */
#define XXH_IMPLEMENTATION /* access definitions */
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <stdatomic.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>

#ifndef CUCKOO_NESTS_PER_BUCKET
#define CUCKOO_NESTS_PER_BUCKET 4
#endif
//Should be 8 * n - 1
#ifndef CUCKOO_FINGERPRINT_SIZE
#define CUCKOO_FINGERPRINT_SIZE 15
#endif

static const char *_cuckoo_errstr[] = {
	[CUCKOO_FILTER_OK] = "OK",
	[CUCKOO_FILTER_NOT_FOUND] = "Not found",
	[CUCKOO_FILTER_FULL] = "full",
	[CUCKOO_FILTER_ALLOCATION_FAILED] = "allocation failed",
	[CUCKOO_FILTER_BUSY] = "Filter is busy, retry after some time",
	[CUCKOO_FILTER_RETRY] = "Retry"
};

const char *cuckoo_strerr(CUCKOO_FILTER_RETURN errnum)
{
	return _cuckoo_errstr[errnum];
}

static inline uint32_t hash(const uint8_t *, uint32_t, uint32_t, uint32_t,
			    uint32_t);

typedef struct {
	uint16_t fingerprint : CUCKOO_FINGERPRINT_SIZE;
	bool marked : 1;
} __attribute__((packed, aligned(2))) cuckoo_nest_t;

typedef struct {
	uint32_t fingerprint;
	uint32_t h1;
	uint32_t h2;
	uint32_t padding;
} __attribute__((packed)) cuckoo_item_t;

typedef struct {
	bool was_found;
	cuckoo_item_t item;
} cuckoo_result_t;

struct cuckoo_filter_t {
	const char *name;
	uint32_t fd;
	sem_t *semid;
	uint32_t bucket_count;
	uint32_t mask;
	uint32_t max_kick_attempts;
	uint32_t seed;
	uint32_t padding;
	cuckoo_nest_t bucket[1];
};

static inline size_t next_power_of_two(size_t x)
{
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	if (8 == sizeof(size_t)) {
		x |= x >> 32;
	}

	return ++x;
}

static inline CUCKOO_FILTER_RETURN
add_fingerprint_to_bucket(cuckoo_filter_t *filter, uint32_t fp, uint32_t h)
{
	for (size_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
		if (0 == filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				 .fingerprint) {
			filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				.fingerprint = fp;
			return CUCKOO_FILTER_OK;
		}
	}

	return CUCKOO_FILTER_FULL;

} /* add_fingerprint_to_bucket() */

static inline CUCKOO_FILTER_RETURN
remove_fingerprint_from_bucket(cuckoo_filter_t *filter, uint32_t fp, uint32_t h)
{
	for (size_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
		if (fp == filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				  .fingerprint) {
			filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				.fingerprint = 0;
			return CUCKOO_FILTER_OK;
		}
	}

	return CUCKOO_FILTER_NOT_FOUND;

} /* remove_fingerprint_from_bucket() */

static inline CUCKOO_FILTER_RETURN
cuckoo_filter_relocate(cuckoo_filter_t *filter, uint32_t fingerprint,
		       uint32_t h1, uint32_t *depth)
{
	uint32_t h2 =
		((h1 ^ hash((const uint8_t *)&fingerprint, sizeof(fingerprint),
			    filter->bucket_count, 900, filter->seed)) %
		 filter->bucket_count);

	if (CUCKOO_FILTER_OK ==
	    add_fingerprint_to_bucket(filter, fingerprint, h1)) {
		return CUCKOO_FILTER_OK;
	}

	if (CUCKOO_FILTER_OK ==
	    add_fingerprint_to_bucket(filter, fingerprint, h2)) {
		return CUCKOO_FILTER_OK;
	}

	bool done_trying = false;
	bool hash_table = (rand() % 2);
	size_t start_col = rand() % CUCKOO_NESTS_PER_BUCKET;
	size_t col = start_col;
KICK:

	if (filter->max_kick_attempts == *depth) {
		return CUCKOO_FILTER_FULL;
	}

	// Select next nest
	col++;
	col = col % CUCKOO_NESTS_PER_BUCKET;

	if (col == start_col) {
		if (done_trying) {
			return CUCKOO_FILTER_RETRY;
		}
		hash_table = !hash_table;
		done_trying = true;
	}

	size_t row = hash_table ? h1 : h2;

	size_t idy = (row * CUCKOO_NESTS_PER_BUCKET);
	size_t idx = (row * CUCKOO_NESTS_PER_BUCKET) + col;
	if (filter->bucket[idy].marked) {
		return CUCKOO_FILTER_RETRY;
	}

	size_t elem = filter->bucket[idx].fingerprint;
	filter->bucket[idy].marked = true;

	CUCKOO_FILTER_RETURN ret;
	if ((ret = cuckoo_filter_relocate(filter, elem, row, depth)) !=
	    CUCKOO_FILTER_OK) {
		filter->bucket[idy].marked = false;
		if (ret == CUCKOO_FILTER_RETRY) {
			(*depth)++;
			goto KICK;
		}
		return CUCKOO_FILTER_FULL;
	}
	filter->bucket[idy].marked = false;
	filter->bucket[idx].fingerprint = fingerprint;

	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_shm_new(const char *name, cuckoo_filter_t **filter,
		      size_t max_key_count, size_t max_kick_attempts,
		      uint32_t seed)
{
	cuckoo_filter_t *new_filter;
	int32_t fd;

	// Default to time based seed
	if (!seed) {
		seed = (size_t)time(NULL);
	}

	size_t bucket_count =
		next_power_of_two(max_key_count / CUCKOO_NESTS_PER_BUCKET);

	if (0.96 <
	    (double)max_key_count / bucket_count / CUCKOO_NESTS_PER_BUCKET) {
		bucket_count <<= 1;
	}

	/* FIXME: Should check for integer overflows here */
	size_t allocation_in_bytes = (sizeof(cuckoo_filter_t) +
				      (bucket_count * CUCKOO_NESTS_PER_BUCKET *
				       sizeof(cuckoo_nest_t)));

	fd = shm_open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		return CUCKOO_FILTER_ALLOCATION_FAILED;
	}
	if (ftruncate(fd, allocation_in_bytes) == -1) {
		close(fd);
		unlink(name);
		return CUCKOO_FILTER_ALLOCATION_FAILED;
	}
	new_filter =
		mmap(NULL, allocation_in_bytes, PROT_WRITE, MAP_SHARED, fd, 0);
	memset(new_filter, 0, allocation_in_bytes);

	if (!new_filter) {
		return CUCKOO_FILTER_ALLOCATION_FAILED;
	}

	new_filter->name = strdup(name);
	new_filter->fd = fd;

	new_filter->bucket_count = bucket_count;
	new_filter->max_kick_attempts = max_kick_attempts;
	new_filter->seed = seed;
	new_filter->mask = (uint32_t)((1U << CUCKOO_FINGERPRINT_SIZE) - 1);
	sem_unlink(name);
	new_filter->semid = sem_open(name, O_CREAT,
				     S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH, 1);
	if (new_filter->semid == SEM_FAILED) {
		printf("%s:%d:%s\n", name, errno, strerror(errno));
		new_filter->semid = NULL;
		cuckoo_filter_free(&new_filter);
		return CUCKOO_FILTER_SEMERR;
	}
	*filter = new_filter;

	return CUCKOO_FILTER_OK;
}
CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, size_t max_key_count,
		  size_t max_kick_attempts, uint32_t seed)
{
	cuckoo_filter_t *new_filter;

	// Default to time based seed
	if (!seed) {
		seed = (size_t)time(NULL);
	}

	size_t bucket_count =
		next_power_of_two(max_key_count / CUCKOO_NESTS_PER_BUCKET);

	if (0.96 <
	    (double)max_key_count / bucket_count / CUCKOO_NESTS_PER_BUCKET) {
		bucket_count <<= 1;
	}

	/* FIXME: Should check for integer overflows here */
	size_t allocation_in_bytes = (sizeof(cuckoo_filter_t) +
				      (bucket_count * CUCKOO_NESTS_PER_BUCKET *
				       sizeof(cuckoo_nest_t)));

	new_filter = calloc(allocation_in_bytes, 1);
	if (!new_filter) {
		return CUCKOO_FILTER_ALLOCATION_FAILED;
	}

	new_filter->name = NULL;

	new_filter->bucket_count = bucket_count;
	new_filter->max_kick_attempts = max_kick_attempts;
	new_filter->seed = seed;
	new_filter->mask = (uint32_t)((1U << CUCKOO_FINGERPRINT_SIZE) - 1);
	new_filter->semid = NULL;

	*filter = new_filter;

	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_free(cuckoo_filter_t **filter)
{
	if ((*filter)->name) {
		close((*filter)->fd);
		if ((*filter)->semid) {
			sem_close((*filter)->semid);
			if (sem_unlink((*filter)->name)) {
				printf("(%s)%d:%s\n", (*filter)->name, errno,
				       strerror(errno));
			}
		}
		shm_unlink((*filter)->name);
	} else {
		free(*filter);
	}
	*filter = NULL;

	return CUCKOO_FILTER_OK;
}

static inline CUCKOO_FILTER_RETURN cuckoo_filter_lookup(cuckoo_filter_t *filter,
							cuckoo_result_t *result,
							const uint8_t *key,
							size_t key_bytelen)
{
	uint32_t _h1 = XXH3_64bits_withSeed(key, key_bytelen, filter->seed);
	uint32_t _h2 = XXH3_64bits_withSeed(key, key_bytelen, _h1);

	uint32_t fingerprint = (_h1 + (1000 * _h2)) % filter->bucket_count;
	fingerprint &= filter->mask;
	fingerprint += !fingerprint;

	uint32_t h1 = _h1 % filter->bucket_count;
	uint32_t h2 = _h2 % filter->bucket_count;

	result->was_found = false;
	result->item.fingerprint = 0;
	result->item.h1 = 0;
	result->item.h2 = 0;

	size_t idx1 = h1 * CUCKOO_NESTS_PER_BUCKET;
	size_t idx2 = h1 * CUCKOO_NESTS_PER_BUCKET;
	for (size_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
		if (fingerprint == filter->bucket[idx1 + ii].fingerprint &&
		    fingerprint == filter->bucket[idx2 + ii].fingerprint) {
			result->was_found = true;
			break;
		}
	}

	result->item.fingerprint = fingerprint;
	result->item.h1 = h1;
	result->item.h2 = h2;

	return (result->was_found ? CUCKOO_FILTER_OK : CUCKOO_FILTER_NOT_FOUND);
}

static CUCKOO_FILTER_RETURN internal_cuckoo_filter_add(cuckoo_filter_t *filter,
						       const uint8_t *key,
						       size_t key_bytelen)
{
	uint32_t _h1 = XXH3_64bits_withSeed(key, key_bytelen, filter->seed);
	uint32_t _h2 = XXH3_64bits_withSeed(key, key_bytelen, _h1);

	uint32_t fingerprint = (_h1 + (1000 * _h2)) % filter->bucket_count;
	fingerprint &= filter->mask;
	fingerprint += !fingerprint;

	uint32_t h1 = _h1 % filter->bucket_count;

	uint32_t depth = 0;
	if (cuckoo_filter_relocate(filter, fingerprint, h1, &depth) !=
	    CUCKOO_FILTER_OK) {
		return CUCKOO_FILTER_FULL;
	}
	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_blocking_add(cuckoo_filter_t *filter, const uint8_t *key,
			   size_t key_bytelen)
{
	if (filter->semid) {
		if (sem_wait(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}

	CUCKOO_FILTER_RETURN ret =
		internal_cuckoo_filter_add(filter, key, key_bytelen);

	if (filter->semid) {
		if (sem_post(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return ret;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const uint8_t *key,
		  size_t key_bytelen)
{
	if (filter->semid) {
		if (sem_trywait(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}

	CUCKOO_FILTER_RETURN ret =
		internal_cuckoo_filter_add(filter, key, key_bytelen);

	if (filter->semid) {
		if (sem_post(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return ret;
}

CUCKOO_FILTER_RETURN
internal_cuckoo_filter_remove(cuckoo_filter_t *filter, const uint8_t *key,
			      size_t key_bytelen)
{
	cuckoo_result_t result;
	bool was_deleted = false;

	cuckoo_filter_lookup(filter, &result, key, key_bytelen);
	if (false == result.was_found) {
		return CUCKOO_FILTER_NOT_FOUND;
	}

	if (CUCKOO_FILTER_OK ==
	    remove_fingerprint_from_bucket(filter, result.item.fingerprint,
					   result.item.h1)) {
		was_deleted = true;
	} else if (CUCKOO_FILTER_OK ==
		   remove_fingerprint_from_bucket(
			   filter, result.item.fingerprint, result.item.h2)) {
		was_deleted = true;
	}

	return ((true == was_deleted) ? CUCKOO_FILTER_OK :
					      CUCKOO_FILTER_NOT_FOUND);
}
CUCKOO_FILTER_RETURN
cuckoo_filter_blocking_remove(cuckoo_filter_t *filter, const uint8_t *key,
			      size_t key_bytelen)
{
	if (filter->semid) {
		if (sem_wait(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}

	CUCKOO_FILTER_RETURN ret =
		internal_cuckoo_filter_remove(filter, key, key_bytelen);

	if (filter->semid) {
		if (sem_post(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return ret;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const uint8_t *key,
		     size_t key_bytelen)
{
	if (filter->semid) {
		if (sem_trywait(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}

	CUCKOO_FILTER_RETURN ret =
		internal_cuckoo_filter_remove(filter, key, key_bytelen);

	if (filter->semid) {
		if (sem_post(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return ret;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const uint8_t *key,
		       size_t key_bytelen)
{
	cuckoo_result_t result;
	return cuckoo_filter_lookup(filter, &result, key, key_bytelen);
}
CUCKOO_FILTER_RETURN
cuckoo_filter_lock(cuckoo_filter_t *filter)
{
	if (filter->semid) {
		if (sem_wait(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return CUCKOO_FILTER_OK;
}
CUCKOO_FILTER_RETURN
cuckoo_filter_unlock(cuckoo_filter_t *filter)
{
	if (filter->semid) {
		if (sem_post(filter->semid)) {
			return CUCKOO_FILTER_BUSY;
		}
	}
	return CUCKOO_FILTER_OK;
}

static inline uint32_t hash(const uint8_t *key, uint32_t key_bytelen,
			    uint32_t size, uint32_t n, uint32_t seed)
{
	uint32_t h1 = XXH3_64bits_withSeed(key, key_bytelen, seed);
	uint32_t h2 = XXH3_64bits_withSeed(key, key_bytelen, h1);

	return ((h1 + (n * h2)) % size);
}
