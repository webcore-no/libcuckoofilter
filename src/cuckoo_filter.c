#include "../include/cuckoo_filter.h"
#define XXH_STATIC_LINKING_ONLY   /* access advanced declarations */
#define XXH_IMPLEMENTATION   /* access definitions */
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

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
	atomic_flag is_busy;
	uint32_t bucket_count;
	uint32_t nests_per_bucket;
	uint32_t mask;
	uint32_t max_kick_attempts;
	uint32_t seed;
	uint32_t padding;
	cuckoo_nest_t bucket[1];
} __attribute__((packed));

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
	for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
		if(0 == filter->bucket[(h * filter->nests_per_bucket) + ii].fingerprint) {
			filter->bucket[(h * filter->nests_per_bucket) + ii].fingerprint = fp;
			return CUCKOO_FILTER_OK;
		}
	}

	return CUCKOO_FILTER_FULL;

} /* add_fingerprint_to_bucket() */

static inline CUCKOO_FILTER_RETURN
remove_fingerprint_from_bucket(cuckoo_filter_t *filter, uint32_t fp, uint32_t h)
{
	for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
		if (fp == filter->bucket[(h * filter->nests_per_bucket) + ii].fingerprint) {
			filter->bucket[(h * filter->nests_per_bucket) + ii].fingerprint = 0;
			return CUCKOO_FILTER_OK;
		}
	}

	return CUCKOO_FILTER_NOT_FOUND;

} /* remove_fingerprint_from_bucket() */

static inline CUCKOO_FILTER_RETURN
cuckoo_filter_relocate(cuckoo_filter_t *filter, uint32_t fingerprint,
		       uint32_t h1, uint32_t *depth)
{
	uint32_t h2 = ((h1 ^ hash(&fingerprint, sizeof(fingerprint),
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
	size_t start_col = rand() % filter->nests_per_bucket;
	size_t col = start_col;
KICK:

	if (filter->max_kick_attempts == *depth) {
		return CUCKOO_FILTER_FULL;
	}

	// Select next nest
	col++;
	col = col % filter->nests_per_bucket;

	if(col == start_col) {
		if(done_trying) {
			return CUCKOO_FILTER_RETRY;
		}
		hash_table = !hash_table;
		done_trying = true;
	}

	size_t row = hash_table ? h1 : h2;


	size_t idy = (row * filter->nests_per_bucket);
	size_t idx = (row * filter->nests_per_bucket) + col;
	if (filter->bucket[idy].marked) {
		return CUCKOO_FILTER_RETRY;
	}

	size_t elem = filter->bucket[idx].fingerprint;
	filter->bucket[idy].marked = true;

	CUCKOO_FILTER_RETURN ret;
	if ((ret = cuckoo_filter_relocate(filter, elem, row, depth)) !=
	    CUCKOO_FILTER_OK) {
		filter->bucket[idy].marked = false;
		if(ret == CUCKOO_FILTER_RETRY) {
			(*depth)++;
			goto KICK;
		}
		return CUCKOO_FILTER_FULL;
	}
	filter->bucket[idy].marked = false;
	filter->bucket[idx].fingerprint = fingerprint;

	return CUCKOO_FILTER_OK;
}

#ifdef CUCKOO_SHM
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#define SHMNAME "CUCKOOFILTERSHM"
static void *malloc_shm(size_t len) {
	int fd = shm_open(SHMNAME, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if(fd == -1) {
		return NULL;
	}
	if(ftruncate(fd, len) == -1) {
		return NULL;
	}
	return mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
}

#endif

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

#ifdef CUCKOO_SHM
	new_filter = malloc_shm(allocation_in_bytes);
	memset(new_filter, 0, allocation_in_bytes);
#else
	new_filter = calloc(allocation_in_bytes, 1);
#endif
	if (!new_filter) {
		return CUCKOO_FILTER_ALLOCATION_FAILED;
	}

	new_filter->bucket_count = bucket_count;
	new_filter->nests_per_bucket = CUCKOO_NESTS_PER_BUCKET;
	new_filter->max_kick_attempts = max_kick_attempts;
	new_filter->seed = seed;
	new_filter->mask = (uint32_t)((1U << CUCKOO_FINGERPRINT_SIZE) - 1);
	atomic_flag_clear(&new_filter->is_busy);

	*filter = new_filter;

	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_free(cuckoo_filter_t **filter)
{
	free(*filter);
	*filter = NULL;

	return CUCKOO_FILTER_OK;
}

static inline CUCKOO_FILTER_RETURN
cuckoo_filter_lookup(cuckoo_filter_t *filter, cuckoo_result_t *result,
		     const uint8_t *key, size_t key_length_in_bytes)
{
	uint32_t fingerprint = hash(key, key_length_in_bytes,
				    filter->bucket_count, 1000, filter->seed);
	uint32_t h1 = hash(key, key_length_in_bytes, filter->bucket_count, 0,
			   filter->seed);

	fingerprint &= filter->mask;
	fingerprint += !fingerprint;
	uint32_t h2 = ((h1 ^ hash(&fingerprint, sizeof(fingerprint),
				  filter->bucket_count, 900, filter->seed)) %
		       filter->bucket_count);

	result->was_found = false;
	result->item.fingerprint = 0;
	result->item.h1 = 0;
	result->item.h2 = 0;

	for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
		size_t idx1 = (h1 * filter->nests_per_bucket) + ii;
		if (fingerprint == filter->bucket[idx1].fingerprint) {
			result->was_found = true;
			break;
		}

		size_t idx2 = (h2 * filter->nests_per_bucket) + ii;
		if (fingerprint == filter->bucket[idx2].fingerprint) {
			result->was_found = true;
			break;
		}
	}

	result->item.fingerprint = fingerprint;
	result->item.h1 = h1;
	result->item.h2 = h2;

	return ((true == result->was_found) ? CUCKOO_FILTER_OK :
						    CUCKOO_FILTER_NOT_FOUND);
}

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const uint8_t *key,
		  size_t key_length_in_bytes)
{
	CUCKOO_FILTER_RETURN ret = CUCKOO_FILTER_OK;
	if(atomic_flag_test_and_set(&filter->is_busy)) {
		return CUCKOO_FILTER_BUSY;
	}

	uint32_t fingerprint = hash(key, key_length_in_bytes,
				    filter->bucket_count, 1000, filter->seed);
	uint32_t h1 = hash(key, key_length_in_bytes, filter->bucket_count, 0,
			   filter->seed);
	uint32_t depth = 0;
	fingerprint &= filter->mask;
	fingerprint += !fingerprint;
	if(cuckoo_filter_relocate(filter, fingerprint,
				  h1, &depth) != CUCKOO_FILTER_OK)
	{

		ret = CUCKOO_FILTER_FULL;
		goto RET;
	}
RET:
	atomic_flag_clear(&filter->is_busy);
	return ret;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const uint8_t *key,
		     size_t key_length_in_bytes)
{
	CUCKOO_FILTER_RETURN ret = CUCKOO_FILTER_OK;
	if(atomic_flag_test_and_set(&filter->is_busy)) {
		return CUCKOO_FILTER_BUSY;
	}
	cuckoo_result_t result;
	bool was_deleted = false;

	cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
	if (false == result.was_found) {
		ret = CUCKOO_FILTER_NOT_FOUND;
		goto RET;
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

	ret = ((true == was_deleted) ? CUCKOO_FILTER_OK :
					      CUCKOO_FILTER_NOT_FOUND);
RET:
	atomic_flag_clear(&filter->is_busy);
	return ret;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const uint8_t *key,
		       size_t key_length_in_bytes)
{
	cuckoo_result_t result;
	return cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
}

static inline uint32_t hash(const const uint8_t *key, uint32_t key_length_in_bytes,
			    uint32_t size, uint32_t n, uint32_t seed)
{
	uint32_t h1 = XXH3_64bits_withSeed(key, key_length_in_bytes, seed);
	uint32_t h2 = XXH3_64bits_withSeed(key, key_length_in_bytes, h1);

	return ((h1 + (n * h2)) % size);
}
