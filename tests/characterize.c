#include <stdio.h>
#include "../include/cuckoo_filter.h"
#include <stdlib.h>

char *domain[] = { ".com", ".org", ".no", ".dk" };

char *subdomain[] = { "www.", "foobar.", "" };

char data[512];
const char *random_string()
{
	char *d = data;
	//d += sprintf(d, "%s", subdomain[rand()%3]);
	size_t len = 3 + rand() % 30;
	for (int i = 0; i < len; i++) {
		d += sprintf(d, "%c", 97 + rand() % 25);
	}

	d += sprintf(d, "%s", domain[rand() % 4]);
	return data;
}

typedef struct sample_s {
	int dupes;
	int elements;
	struct sample_s *next;
} sample_t;

sample_t *insert_until_full(int elements, int depth, int sample_rate)
{
	cuckoo_filter_t *filter;
	sample_t *root;
	sample_t *head;
	root = head = calloc(sizeof(sample_t), 1);

	bool rc;

	rc = cuckoo_filter_new(&filter, elements, depth, 0);

	int i;
	int errors;
	int dupes = 0;
	CUCKOO_FILTER_RETURN ret;

	for (i = 0;; i++) {
		char *d = random_string();
		ret = cuckoo_filter_add(filter, d, strlen(d));
		if (ret == CUCKOO_FILTER_DUP) {
			dupes++;
		}
		if (ret == CUCKOO_FILTER_FULL) {
			head->dupes = dupes;
			head->elements = i;
			break;
		}
		if (i%sample_rate == 0) {
			head->dupes = dupes;
			head->elements = i;
			head = head->next = calloc(sizeof(sample_t), 1);
		}
	}
	if (cuckoo_filter_free(&filter)) {
		printf("%s/%d: %d\n", __func__, __LINE__, rc);
	}
	return root;
}

#define ELEMENTS 1000000
int main(void)
{
	double old, old_dup, old_dup_rate;
	sample_t *sample = insert_until_full(ELEMENTS, 100, 20000);
	while(sample) {
		printf("%d, %d\n", sample->elements, sample->dupes);
		sample = sample->next;
	}
	return 0;
}
