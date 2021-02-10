#include "../include/cuckoo_filter.h"
#define FILTER_COUNT 10000

int main(void)
{
	cuckoo_filter_t *filters[FILTER_COUNT];
	bool rc;
	printf("NEW\n");
	for (int i = 0; i < FILTER_COUNT; i++) {
		rc = cuckoo_filter_new(&(filters[i]), 100, 100,
				       (uint32_t)(time(NULL) & 0xffffffff));
		if (CUCKOO_FILTER_OK != rc) {
			printf("%s/%d: %d\n", __func__, __LINE__, rc);
		}
		printf("%d ", i);
	}

	printf("CONTAINS\n");
	for (int i = 0; i < FILTER_COUNT; i++) {
		rc = cuckoo_filter_contains(filters[i], "123456789012345676890",
					    20);
		if (CUCKOO_FILTER_OK == rc) {
			printf("%s/%d: %d\n", __func__, __LINE__, rc);
		}
		printf("%d ", i);
	}

	printf("ADD\n");
	for (int i = 0; i < FILTER_COUNT; i++) {
		rc = cuckoo_filter_add(filters[i], "123456789012345676890", 20);
		if (CUCKOO_FILTER_OK != rc) {
			printf("%s/%d: %d\n", __func__, __LINE__, rc);
		}
		printf("%d ", i);
	}

	printf("FREE\n");
	for (int i = 0; i < FILTER_COUNT; i++) {
		rc = cuckoo_filter_free(&(filters[i]));
		printf("%d ", i);
	}

	return 0;

} /* main() */

