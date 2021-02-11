#include <pthread.h>
#include "../include/cuckoo_filter.h"

#define RANGE 10000000

void *add_to_filter(cuckoo_filter_t *filter)
{
	int i, rc;
	int dups;
	for(i = RANGE; i < RANGE * 2; i++) {
		if((rc = cuckoo_filter_add(filter, &i, sizeof(i)))) {
			printf("[ERROR][%s]: %d\n", cuckoo_strerr(rc), i);
		}
		if(i % (RANGE / 100) == 0) { printf("adding %d\n", i); }
	}
        return NULL;
}

void filter_contains_range(cuckoo_filter_t *filter, int start, int n)
{
	int i, rc;
	for(i = start; i < start + n; i++) {
		if(cuckoo_filter_contains(filter, &i, sizeof(i))) {
			printf("Filter does not contain : %d\n", i);
		}
		if(i % (RANGE / 100) == 0) { printf("looking for %d\n", i); }
	}
}

void remove_from_filter(cuckoo_filter_t *filter)
{
	int i, rc;
	for(i = RANGE; i < RANGE * 2; i++) {
		if(cuckoo_filter_remove(filter, &i, sizeof(i))) {
			printf("Failed to remove: %d\n", i);
		}
	}
}

int main(void)
{
	cuckoo_filter_t *filter;
	bool             rc;

	rc = cuckoo_filter_new(&filter, RANGE * 4, 200, 0);
	if(CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	// Add the once we are gone read
	int i;
	for(i = 0; i < RANGE; i++) {
		rc = cuckoo_filter_add(filter, &i, sizeof(i));
		if(CUCKOO_FILTER_OK != rc) {
			printf("%s/%d: %d\n", __func__, __LINE__, rc);
		}
	}
	pthread_t add_thread;
	// Spawn a pthread to add more elements we dont care about
	if(pthread_create(&add_thread, NULL, add_to_filter, filter)) {
		fprintf(stderr, "Error creating thread\n");
		return 1;
	}

	filter_contains_range(filter, 0, RANGE);

	pthread_join(add_thread, NULL);
	//filter_contains_range(filter, RANGE, RANGE);
	rc = cuckoo_filter_free(&filter);
	if(CUCKOO_FILTER_OK != rc) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}

	return 0;

} /* main() */

