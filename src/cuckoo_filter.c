#include "../include/cuckoo_filter.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define CUCKOO_NESTS_PER_BUCKET 4

//Should be 8 * n - 1
#define CUCKOO_FINGERPRINT_SIZE 15

static const char *_cuckoo_errstr[] = {
	[CUCKOO_FILTER_OK] = "OK",
	[CUCKOO_FILTER_NOT_FOUND] = "Not found",
	[CUCKOO_FILTER_FULL] = "full",
	[CUCKOO_FILTER_ALLOCATION_FAILED] = "allocation failed",
	[CUCKOO_FILTER_DUP] = "duplicate",
};

const char *cuckoo_strerr(CUCKOO_FILTER_RETURN errnum)
{
	return _cuckoo_errstr[errnum];
}

static inline uint32_t murmurhash(const void *, uint32_t, uint32_t);
static inline uint32_t hash(const void *, uint32_t, uint32_t, uint32_t,
			    uint32_t);

typedef volatile struct {
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
	uint32_t bucket_count;
	uint32_t nests_per_bucket;
	uint32_t mask;
	uint32_t max_kick_attempts;
	uint32_t seed;
	uint32_t padding;
	cuckoo_item_t victim;
	cuckoo_item_t *last_victim;
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
		       uint32_t h1, uint32_t depth)
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

KICK:
	if (filter->max_kick_attempts == depth) {
		return CUCKOO_FILTER_FULL;
	}

	// Randomly select a hash
	size_t row = (0 == (rand() % 2) ? h1 : h2);

	// Randomly select a nest
	size_t col = (rand() % filter->nests_per_bucket);

	size_t idx = (row * filter->nests_per_bucket) + col;
	if (filter->bucket[idx].marked) {
		depth++;
		goto KICK;
	}

	size_t elem = filter->bucket[idx].fingerprint;
	filter->bucket[idx].marked = true;

	if (cuckoo_filter_relocate(filter, elem, row, (depth + 1)) !=
	    CUCKOO_FILTER_OK) {
		filter->bucket[idx].marked = false;
		return CUCKOO_FILTER_FULL;
	}
	filter->bucket[idx].marked = !filter->bucket[idx].marked;
	filter->bucket[idx].fingerprint = fingerprint;

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

	new_filter->last_victim = NULL;
	memset(&new_filter->victim, 0, sizeof(new_filter)->victim);
	new_filter->bucket_count = bucket_count;
	new_filter->nests_per_bucket = CUCKOO_NESTS_PER_BUCKET;
	new_filter->max_kick_attempts = max_kick_attempts;
	new_filter->seed = seed;
	new_filter->mask = (uint32_t)((1U << CUCKOO_FINGERPRINT_SIZE) - 1);

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
		     void *key, size_t key_length_in_bytes)
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
cuckoo_filter_add(cuckoo_filter_t *filter, void *key,
		  size_t key_length_in_bytes)
{
	cuckoo_result_t result;
	cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
	if (true == result.was_found) {
		return CUCKOO_FILTER_DUP;
	}

	if (NULL != filter->last_victim) {
		return CUCKOO_FILTER_FULL;
	}
	return cuckoo_filter_relocate(filter, result.item.fingerprint,
				  result.item.h1, 0);
}

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, void *key,
		     size_t key_length_in_bytes)
{
	cuckoo_result_t result;
	bool was_deleted = false;

	cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
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

	if ((true == was_deleted) & (NULL != filter->last_victim)) {
	}

	return ((true == was_deleted) ? CUCKOO_FILTER_OK :
					      CUCKOO_FILTER_NOT_FOUND);
}

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, void *key,
		       size_t key_length_in_bytes)
{
	cuckoo_result_t result;
	return cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
}

static inline uint32_t murmurhash(const void *key, uint32_t key_length_in_bytes,
				  uint32_t seed)
{
	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;
	uint32_t r1 = 15;
	uint32_t r2 = 13;
	uint32_t m = 5;
	uint32_t n = 0xe6546b64;
	uint32_t h = 0;
	uint32_t k = 0;
	uint8_t *d = (uint8_t *)key;
	const uint32_t *chunks = NULL;
	const uint8_t *tail = NULL;
	int i = 0;
	int l = (key_length_in_bytes / sizeof(uint32_t));

	h = seed;

	chunks = (const uint32_t *)(d + l * sizeof(uint32_t));
	tail = (const uint8_t *)(d + l * sizeof(uint32_t));

	for (i = -l; i != 0; ++i) {
		k = chunks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
		h ^= k;
		h = (h << r2) | (h >> (32 - r2));
		h = h * m + n;
	}

	k = 0;
	switch (key_length_in_bytes & 3) {
	case 3:
		k ^= (tail[2] << 16);
	case 2:
		k ^= (tail[1] << 8);
	case 1:
		k ^= tail[0];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
		h ^= k;
	}

	h ^= key_length_in_bytes;
	h ^= (h >> 16);
	h *= 0x85ebca6b;
	h ^= (h >> 13);
	h *= 0xc2b2ae35;
	h ^= (h >> 16);

	return h;
}

static inline uint32_t hash(const void *key, uint32_t key_length_in_bytes,
			    uint32_t size, uint32_t n, uint32_t seed)
{
	uint32_t h1 = murmurhash(key, key_length_in_bytes, seed);
	uint32_t h2 = murmurhash(key, key_length_in_bytes, h1);

	return ((h1 + (n * h2)) % size);
}
