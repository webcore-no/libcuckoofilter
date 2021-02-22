#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>
#include "../include/cuckoo_filter.h"

int main(void)
{
	cuckoo_filter_t *filter;
	bool rc;

	const uint8_t *key = (const uint8_t *)"shm_test_key";
	uint64_t key_len = strlen((const char *)key);

	rc = cuckoo_filter_new(&filter, 500000, 100,
			       (uint32_t)(time(NULL) & 0xffffffff),
			       cuckoo_filter_shm_alloc);
	assert(rc == CUCKOO_FILTER_OK);

	pid_t pid = fork();
	assert(pid >= 0);

	if (!pid) {
		//Child
		rc = cuckoo_filter_add(filter, key, key_len);
		assert(rc == CUCKOO_FILTER_OK);
		exit(0);
	}
	waitpid(pid, NULL, 0);

	rc = cuckoo_filter_contains(filter, key, key_len);
	assert(rc == CUCKOO_FILTER_OK);

	rc = cuckoo_filter_shm_free(&filter);
	assert(rc == CUCKOO_FILTER_OK);

	return 0;
}
