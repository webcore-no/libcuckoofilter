#include "../include/cuckoo_filter.h"

#define ELEMENTS 460000
int main (void) {
  cuckoo_filter_t  *filter;
  bool              rc;

  printf("%d", CUCKOO_FINGERPRINT_SIZE);
  rc = cuckoo_filter_new(&filter, 500000, 100,
    (uint32_t) (time(NULL) & 0xffffffff));
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

  int i;
  for (i = 0; i < ELEMENTS; i++) {
    rc = cuckoo_filter_add(filter, &i, sizeof(i));
    if (CUCKOO_FILTER_OK != rc) {
      printf("add: %d:%d\n", rc, i);
      exit(1);
    }
  }
  for(int j = 1; j < 10; j++) {
  for (i = 0; i < ELEMENTS; i++) {
    rc = cuckoo_filter_contains(filter, &i, sizeof(i));
    if (CUCKOO_FILTER_OK != rc) {
      printf("contains: %d %d\n",  rc, i);
      return 1;
    }
  }
  }

  rc = cuckoo_filter_free(&filter);
  if (CUCKOO_FILTER_OK != rc) {
    printf("%s/%d: %d\n", __func__, __LINE__, rc);
  }

  return 0;

} /* main() */

