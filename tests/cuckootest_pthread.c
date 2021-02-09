#include <pthread.h>
#include "../include/cuckoo_filter.h"

#define RANGE 460000

void add_to_filter(cuckoo_filter_t *filter)
{
  int i, rc;
  for (i = RANGE; i < RANGE*2; i++) {
    rc = cuckoo_filter_add(filter, &i, sizeof(i));
    if (CUCKOO_FILTER_OK != rc) {
      printf("%s/%d: %d\n", __func__, __LINE__, rc);
    }
  }
}

void remove_from_filter(cuckoo_filter_t *filter)
{
  int i, rc;
  for (i = RANGE; i < RANGE*2; i++) {
    rc = cuckoo_filter_remove(filter, &i, sizeof(i));
    if (CUCKOO_FILTER_OK != rc) {
      printf("%s/%d: %d\n", __func__, __LINE__, rc);
    }
  }
}

int main (void) {
  cuckoo_filter_t  *filter;
  bool              rc;

  rc = cuckoo_filter_new(&filter, 1000000, 100, NULL);
  if (CUCKOO_FILTER_OK != rc) {
    printf("%s/%d: %d\n", __func__, __LINE__, rc);
  }

  // Add the once we are gone read
  int i;
  for (i = 0; i < RANGE; i++) {
    rc = cuckoo_filter_add(filter, &i, sizeof(i));
    if (CUCKOO_FILTER_OK != rc) {
      printf("%s/%d: %d\n", __func__, __LINE__, rc);
    }
  }
  pthread_t add_thread;
  // Spawn a pthread to add more elements we dont care about
 if(pthread_create(&add_thread, NULL, add_to_filter, filter)) {
    fprintf(stderr, "Error creating thread\n");
    return 1;
  }
 add_to_filter(filter);
   for (i = 0; i < RANGE; i++) {
    rc = cuckoo_filter_contains(filter, &i, sizeof(i));
    if (rc != CUCKOO_FILTER_OK) {
      printf("contains: %d %d\n", rc, i);
    }
  }

  rc = cuckoo_filter_free(&filter);
  if (CUCKOO_FILTER_OK != rc) {
    printf("%s/%d: %d\n", __func__, __LINE__, rc);
  }

  return 0;

} /* main() */

