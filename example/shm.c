#include "../include/cuckoo_filter.h"

int main(void)
{
	cuckoo_filter_t *filter;
	bool rc;

	rc = cuckoo_filter_new(&filter, 500000, 100,
			       (uint32_t)(time(NULL) & 0xffffffff),
			       cuckoo_filter_shm_alloc);
	if (CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}
	rc = cuckoo_filter_contains(filter, "test", 4);
	if (CUCKOO_FILTER_OK == rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	rc = cuckoo_filter_add(filter, "test", 4);
	if (CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}
	rc = cuckoo_filter_contains(filter, "test", 4);
	if (CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	rc = cuckoo_filter_remove(filter, "test", 4);
	if (CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}
	rc = cuckoo_filter_contains(filter, "test", 4);
	if (CUCKOO_FILTER_OK == rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	rc = cuckoo_filter_shm_free(&filter);
	if (CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	return 0;
}

