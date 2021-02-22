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
#ifndef CUCKOO_NESTS_PER_BUCKET
#define CUCKOO_NESTS_PER_BUCKET 4
#endif
//Should be 8 * n - 1
#ifndef CUCKOO_FINGERPRINT_SIZE
#define CUCKOO_FINGERPRINT_SIZE 16
#endif

#define BUCKETS_PER_MARK (sizeof(atomic_char) * 8)

char cuckoo_filter_magic[9] = { 0x2f, 0xa7, 0xe0, 0xca, 0x53,
			       0xc5, 0xd2, 0x01, 0x00 };

static inline uint32_t
hash(const void *, uint32_t, uint32_t, uint32_t, uint32_t);

static inline bool mark(cuckoo_filter_t *filter, uint64_t idx);
static inline bool release_mark(cuckoo_filter_t *filter, uint64_t idx);
typedef struct {
	uint16_t fingerprint;
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
	uint64_t size;
	uint32_t bucket_count;
	uint32_t mask;
	uint32_t max_kick_attempts;
	uint32_t seed;
	uint32_t padding;
	atomic_char *marks;
	cuckoo_nest_t bucket[1];
};

static inline uint64_t next_power_of_two(uint64_t x)
{
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	if (8 == sizeof(uint64_t)) {
		x |= x >> 32;
	}

	return ++x;
}

static inline CUCKOO_FILTER_RETURN
add_fingerprint_to_bucket(cuckoo_filter_t *filter, uint32_t fp, uint32_t h)
{
	for (uint64_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
		if (0 == filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				 .fingerprint) {
			filter->bucket[(h * CUCKOO_NESTS_PER_BUCKET) + ii]
				.fingerprint = fp;
			return CUCKOO_FILTER_OK;
		}
	}
	return CUCKOO_FILTER_FULL;
}

static inline CUCKOO_FILTER_RETURN
remove_fingerprint_from_bucket(cuckoo_filter_t *filter, uint32_t fp, uint32_t h)
{
	for (uint64_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
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
		((h1 ^ hash(&fingerprint, sizeof(fingerprint),
			    filter->bucket_count, 900, filter->seed)) %
		 filter->bucket_count);

	mark(filter, h1);
	if (CUCKOO_FILTER_OK ==
	    add_fingerprint_to_bucket(filter, fingerprint, h1)) {
		release_mark(filter, h1);
		return CUCKOO_FILTER_OK;
	}

	release_mark(filter, h1);
	mark(filter, h2);
	if (CUCKOO_FILTER_OK ==
	    add_fingerprint_to_bucket(filter, fingerprint, h2)) {
		release_mark(filter, h2);
		return CUCKOO_FILTER_OK;
	}
	release_mark(filter, h2);

	bool done_trying = false;
	bool hash_table = (rand() % 2);
	uint64_t start_col = rand() % CUCKOO_NESTS_PER_BUCKET;
	uint64_t col = start_col;
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

	uint64_t row = hash_table ? h1 : h2;

	uint64_t idy = row;
	uint64_t idx = (row * CUCKOO_NESTS_PER_BUCKET) + col;
	if (!mark(filter, idy)) {
		return CUCKOO_FILTER_RETRY;
	}

	uint64_t elem = filter->bucket[idx].fingerprint;

	CUCKOO_FILTER_RETURN ret;
	if ((ret = cuckoo_filter_relocate(filter, elem, row, depth)) !=
	    CUCKOO_FILTER_OK) {
		release_mark(filter, idy);
		if (ret == CUCKOO_FILTER_RETRY) {
			(*depth)++;
			goto KICK;
		}
		return CUCKOO_FILTER_FULL;
	}
	filter->bucket[idx].fingerprint = fingerprint;
	release_mark(filter, idy);
	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed,
		  cuckoo_allocate allocator)
{
	cuckoo_filter_t *new_filter;
	// Default to time based seed
	if (!seed) {
		seed = (uint64_t)time(NULL);
	}

	uint64_t bucket_count =
		next_power_of_two(max_key_count / CUCKOO_NESTS_PER_BUCKET);

	if (0.96 <
	    (double)max_key_count / bucket_count / CUCKOO_NESTS_PER_BUCKET) {
		bucket_count <<= 1;
	}

	/* FIXME: Should check for integer overflows here */
	size_t size = (sizeof(cuckoo_filter_t) +
		       (bucket_count * CUCKOO_NESTS_PER_BUCKET *
			sizeof(cuckoo_nest_t)) +
		       bucket_count / BUCKETS_PER_MARK);

	if (allocator(&new_filter, size) != CUCKOO_FILTER_OK) {
		return CUCKOO_FILTER_ERROR;
	}
	memset(new_filter, 0, size);

	if (!new_filter) {
		return CUCKOO_FILTER_ERROR;
	}

	new_filter->size = size;

	new_filter->bucket_count = bucket_count;
	new_filter->max_kick_attempts = max_kick_attempts;
	new_filter->seed = seed;
	new_filter->mask = (uint32_t)((1U << CUCKOO_FINGERPRINT_SIZE) - 1);

	new_filter->marks = (void *)(new_filter) + size -
			    bucket_count / BUCKETS_PER_MARK;
	*filter = new_filter;

	return CUCKOO_FILTER_OK;
}
CUCKOO_FILTER_RETURN
cuckoo_filter_load(cuckoo_filter_t **filter, int fd, cuckoo_allocate allocator)
{
	char magic[8];
	read(fd, magic, 8);
	if(memcmp(magic, cuckoo_filter_magic, 8) != 0) {
		//Magic is wrong
		return CUCKOO_FILTER_ERROR;
	}
	uint32_t version;
	read(fd, &version, sizeof(uint32_t));

	if(version < 10000) {
		//No load/save before version 1.0.0
		return CUCKOO_FILTER_ERROR;
	}

	if(version > CUCKOO_FILTER_VERSION_ID) {
		//File is newer then current version
		return CUCKOO_FILTER_ERROR;
	}
	uint64_t size;
	read(fd, &size, sizeof(uint64_t));
	if(allocator(filter, size) != CUCKOO_FILTER_OK) {
		return CUCKOO_FILTER_ERROR;
	}
	(*filter)->size = size;
	read(fd, &(*filter)->size + 1, size);
	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN
cuckoo_filter_save(cuckoo_filter_t *filter, int fd)
{
	//write magic
	write(fd, cuckoo_filter_magic, 8);
	//write version
	uint32_t version = CUCKOO_FILTER_VERSION_ID;
	write(fd, &version, sizeof(uint32_t));

	//wirte filter
	write(fd, filter, filter->size);

	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN cuckoo_filter_shm_free(cuckoo_filter_t **filter)
{
	if (munmap(*filter, (*filter)->size) == -1) {
		return CUCKOO_FILTER_ERROR;
	}
	*filter = NULL;
	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN cuckoo_filter_shm_alloc(cuckoo_filter_t **filter,
					     size_t size)
{
	*filter = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_ANON | MAP_SHARED, -1, 0);

	if (*filter == MAP_FAILED) {
		*filter = NULL;
		return CUCKOO_FILTER_ERROR;
	}

	return CUCKOO_FILTER_OK;
}
CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter)
{
	free(*filter);
	*filter = NULL;
	return CUCKOO_FILTER_OK;
}

CUCKOO_FILTER_RETURN cuckoo_filter_alloc(cuckoo_filter_t **filter, size_t size)
{
	*filter = malloc(size);

	if (*filter == NULL) {
		*filter = NULL;
		return CUCKOO_FILTER_ERROR;
	}
	return CUCKOO_FILTER_OK;
}

static inline CUCKOO_FILTER_RETURN cuckoo_filter_lookup(cuckoo_filter_t *filter,
							cuckoo_result_t *result,
							const void *key,
							size_t keylen)
{
	uint32_t _h1 = XXH3_64bits_withSeed(key, keylen, filter->seed);
	uint32_t _h2 = XXH3_64bits_withSeed(key, keylen, _h1);

	uint32_t fingerprint = (_h1 + (1000 * _h2)) % filter->bucket_count;
	fingerprint &= filter->mask;
	fingerprint += !fingerprint;

	uint32_t h1 = _h1 % filter->bucket_count;
	uint32_t h2 =
		((h1 ^ hash(&fingerprint, sizeof(fingerprint),
			    filter->bucket_count, 900, filter->seed)) %
		 filter->bucket_count);

	result->was_found = false;
	result->item.fingerprint = 0;
	result->item.h1 = 0;
	result->item.h2 = 0;

	uint64_t idx1 = h1 * CUCKOO_NESTS_PER_BUCKET;
	uint64_t idx2 = h2 * CUCKOO_NESTS_PER_BUCKET;
	for (uint64_t ii = 0; ii < CUCKOO_NESTS_PER_BUCKET; ++ii) {
		if (filter->bucket[idx1 + ii].fingerprint == fingerprint ||
		    filter->bucket[idx2 + ii].fingerprint == fingerprint) {
			result->was_found = true;
			break;
		}
	}

	result->item.fingerprint = fingerprint;
	result->item.h1 = h1;
	result->item.h2 = h2;

	return (result->was_found ? CUCKOO_FILTER_OK : CUCKOO_FILTER_NOT_FOUND);
}

CUCKOO_FILTER_RETURN cuckoo_filter_add(cuckoo_filter_t *filter,
				       const void *key, size_t keylen)
{
	uint32_t _h1 = XXH3_64bits_withSeed(key, keylen, filter->seed);
	uint32_t _h2 = XXH3_64bits_withSeed(key, keylen, _h1);

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
cuckoo_filter_remove(cuckoo_filter_t *filter, const void *key,
		     size_t keylen)
{
	cuckoo_result_t result;
	bool was_deleted = false;

	cuckoo_filter_lookup(filter, &result, key, keylen);
	if (!result.was_found) {
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
cuckoo_filter_contains(cuckoo_filter_t *filter, const void *key,
		       size_t keylen)
{
	cuckoo_result_t result;
	return cuckoo_filter_lookup(filter, &result, key, keylen);
}

static inline uint32_t hash(const void *key, uint32_t key_bytelen,
			    uint32_t size, uint32_t n, uint32_t seed)
{
	uint32_t h1 = XXH3_64bits_withSeed(key, key_bytelen, seed);
	uint32_t h2 = XXH3_64bits_withSeed(key, key_bytelen, h1);

	return ((h1 + (n * h2)) % size);
}

static inline bool mark(cuckoo_filter_t *filter, uint64_t idx)
{
	if (!filter->marks)
		return 1;
	uint64_t mark_index = idx / BUCKETS_PER_MARK;
	uint64_t bit = 1u << (idx % BUCKETS_PER_MARK);
	return (atomic_fetch_or(&filter->marks[mark_index], bit) & bit) == 0;
}
static inline bool release_mark(cuckoo_filter_t *filter, uint64_t idx)
{
	if (!filter->marks)
		return 1;
	uint64_t mark_index = idx / BUCKETS_PER_MARK;
	uint64_t bit = 1u << (idx % BUCKETS_PER_MARK);
	return (atomic_fetch_and(&filter->marks[mark_index], ~bit) & bit);
}

