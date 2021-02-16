#include "../include/cuckoo_filter.h"
#include <check.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>


// A seed proved not to become FULL
#define SEED 123456789

#define FCK_START(test_name) \
	START_TEST(test_name) { \
		cuckoo_filter_t *filter;\
		CUCKOO_FILTER_RETURN ret;\
		ret = cuckoo_filter_new(&filter, 1000000, 100, SEED);\
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK);

#define FCK_SHM_START(test_name) \
	START_TEST(test_name) { \
		cuckoo_filter_t *filter;\
		CUCKOO_FILTER_RETURN ret;\
		ret = cuckoo_filter_shm_new(#test_name, &filter, 1000000, 100, SEED);\
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK);

#define FCK_END \
		ret = cuckoo_filter_free(&filter); \
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK); \
	} \
	END_TEST


#define FCK_NADD(name, name_len) { \
	ret = cuckoo_filter_add(filter, (uint8_t *)name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_ADD(name) FCK_NADD(name, strlen(name))
#define FCK_PADD(name) FCK_NADD(name, sizeof(*name))

#define FCK_NREMOVE(name, name_len) { \
	ret = cuckoo_filter_remove(filter, (uint8_t *)name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_REMOVE(name) FCK_NREMOVE(name, strlen(name))
#define FCK_PREMOVE(name) FCK_NREMOVE(name, sizeof(*name))

#define FCK_NCONTAINS(name, name_len) { \
	ret = cuckoo_filter_contains(filter, (uint8_t *)name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_CONTAINS(name) FCK_NCONTAINS(name, strlen(name))
#define FCK_PCONTAINS(name) FCK_NCONTAINS(name, sizeof(*name))

#define FCK_NEXCLUDES(name, name_len) \
	{ \
		ret = cuckoo_filter_contains(filter, (uint8_t *)name, name_len); \
		ck_assert_int_eq(ret, CUCKOO_FILTER_NOT_FOUND); \
	}
#define FCK_EXCLUDES(name) FCK_NEXCLUDES(name, strlen(name))
#define FCK_PEXCLUDES(name) FCK_NEXCLUDES(name, sizeof(*name))


START_TEST(test_cuckoofilter_create)
{
	cuckoo_filter_t *filter;
	CUCKOO_FILTER_RETURN ret;
	ret = cuckoo_filter_new(&filter, 500000, 100, 0);
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);
}
END_TEST

FCK_START(test_cuckoofilter_add)
{
	FCK_EXCLUDES("test");
	FCK_ADD("test");
	FCK_CONTAINS("test");
}
FCK_END

FCK_START(test_cuckoofilter_remove)
{
	FCK_EXCLUDES("test");
	FCK_ADD("test");
	FCK_CONTAINS("test");
	FCK_REMOVE("test");
	FCK_EXCLUDES("test");
}
FCK_END

FCK_START(test_cuckoofilter_add_multi)
{
	FCK_EXCLUDES("test");
	FCK_ADD("test");
	FCK_CONTAINS("test");
	FCK_ADD("test");
	FCK_CONTAINS("test");
}
FCK_END

FCK_START(test_cuckoofilter_remove_multi)
{
	FCK_EXCLUDES("test");
	FCK_ADD("test");
	FCK_ADD("test");
	FCK_CONTAINS("test");
	FCK_REMOVE("test");
	FCK_CONTAINS("test");
	FCK_REMOVE("test");
	FCK_EXCLUDES("test");
}
FCK_END

FCK_START(test_cuckoofilter_loop)
{
	for(int i = 0; i < 10000; i++) {
		FCK_PEXCLUDES(&i);
		FCK_PADD(&i);
	}
	for(int i = 0; i < 10000; i++) {
		FCK_PCONTAINS(&i);
	}
	for(int i = 0; i < 10000; i++) {
		FCK_PCONTAINS(&i);
		FCK_PREMOVE(&i);
		FCK_PEXCLUDES(&i);
	}
}
FCK_END

FCK_SHM_START(test_cuckoofilter_forked_add)
{
	for(int i = 0; i < 10000; i++) {
		FCK_PEXCLUDES(&i);
		FCK_PADD(&i);
	}
	pid_t pid = fork();
	ck_assert_int_ge(pid, 0);
	if(pid) {
		//Parent
		// Do some reading while child adds
		for(int i = 0; i < 10000; i++) {
			FCK_PCONTAINS(&i);
		}
		waitpid(pid, NULL);
		// Read values child added
		for(int i = 10000; i < 20000; i++) {
			FCK_PCONTAINS(&i);
		}
	} else {
		// Child
		// Write some values and exit
		for(int i = 10000; i < 20000; i++) {
			cuckoo_filter_add(filter, &i, sizeof(i));
		}
		exit(0);
	}
}
FCK_END


FCK_SHM_START(test_cuckoofilter_forked_remove)
{
	for(int i = 0; i < 20000; i++) {
		FCK_PADD(&i);
	}
	pid_t pid = fork();
	ck_assert_int_ge(pid, 0);
	if(pid) {
		//Parent
		// Do some reading while child adds
		for(int i = 0; i < 10000; i++) {
			FCK_PCONTAINS(&i);
		}
		waitpid(pid, NULL);
		// Read values child added
		for(int i = 10000; i < 20000; i++) {
			FCK_PEXCLUDES(&i);
		}
	} else {
		// Child
		// Write some values and exit
		for(int i = 10000; i < 20000; i++) {
			cuckoo_filter_remove(filter, &i, sizeof(i));
		}
		exit(0);
	}
}
FCK_END

FCK_SHM_START(test_cuckoofilter_forked_remove_and_add)
{
	for(int i = 0; i < 20000; i++) {
		FCK_PADD(&i);
	}
	pid_t pid = fork();
	ck_assert_int_ge(pid, 0);
	if(pid) {
		//Parent
		// Do some reading while child adds
		for(int i = 0; i < 10000; i++) {
			FCK_PCONTAINS(&i);
		}
		waitpid(pid, NULL);
		for(int i = 0; i < 10000; i++) {
			FCK_PCONTAINS(&i);
		}
	} else {
		pid = fork();
		srand(pid ? 123111 : 111321); //Set seed for master and child diffrently
		for(int i = 10000; i < 20000; i++) {
			int j = rand()%10000 + 10000;
			if(rand()%2) {
				cuckoo_filter_blocking_add(filter, &j, sizeof(j));
			} else {
				cuckoo_filter_blocking_remove(filter, &j, sizeof(j));
			}
		}
		if(pid) {
			waitpid(pid, NULL);
		}
		exit(0);
	}
}
FCK_END


Suite *cuckoofilter_suite(void)
{
	Suite *s;
	TCase *tc_core;
	TCase *tc_shm;

	s = suite_create("cuckoofilter");

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_cuckoofilter_create);
	tcase_add_test(tc_core, test_cuckoofilter_add);
	tcase_add_test(tc_core, test_cuckoofilter_remove);
	tcase_add_test(tc_core, test_cuckoofilter_add_multi);
	tcase_add_test(tc_core, test_cuckoofilter_remove_multi);
	tcase_add_test(tc_core, test_cuckoofilter_loop);
	suite_add_tcase(s, tc_core);

	tc_shm = tcase_create("SHM");
	tcase_add_test(tc_shm, test_cuckoofilter_forked_add);
	tcase_add_test(tc_shm, test_cuckoofilter_forked_remove);
	tcase_add_test(tc_shm, test_cuckoofilter_forked_remove_and_add);
	suite_add_tcase(s, tc_shm);
	return s;
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = cuckoofilter_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
