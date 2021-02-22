#include "../include/cuckoo_filter.h"
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


// A seed proved not to become FULL
#define SEED 123456789

#define FCK_START(test_name) \
	START_TEST(test_name) { \
		cuckoo_filter_t *filter;\
		CUCKOO_FILTER_RETURN ret;\
		ret = cuckoo_filter_new(&filter, 1000000, 100, SEED, cuckoo_filter_alloc);\
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK);

#define FCK_SHM_START(test_name) \
	START_TEST(test_name) { \
		cuckoo_filter_t *filter;\
		CUCKOO_FILTER_RETURN ret;\
		ret = cuckoo_filter_new(&filter, 1000000, 100, SEED, cuckoo_filter_shm_alloc);\
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK);

#define FCK_SHM_END \
		ret = cuckoo_filter_shm_free(&filter); \
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK); \
	} \
	END_TEST

#define FCK_END \
		ret = cuckoo_filter_free(&filter); \
		ck_assert_int_eq(ret, CUCKOO_FILTER_OK); \
	} \
	END_TEST


#define FCK_NADD(name, name_len) { \
	ret = cuckoo_filter_add(filter, name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_ADD(name) FCK_NADD(name, strlen(name))
#define FCK_PADD(name) FCK_NADD(name, sizeof(*name))

#define FCK_NREMOVE(name, name_len) { \
	ret = cuckoo_filter_remove(filter, name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_REMOVE(name) FCK_NREMOVE(name, strlen(name))
#define FCK_PREMOVE(name) FCK_NREMOVE(name, sizeof(*name))

#define FCK_NCONTAINS(name, name_len) { \
	ret = cuckoo_filter_contains(filter, name, name_len); \
	ck_assert_int_eq(ret, CUCKOO_FILTER_OK);}
#define FCK_CONTAINS(name) FCK_NCONTAINS(name, strlen(name))
#define FCK_PCONTAINS(name) FCK_NCONTAINS(name, sizeof(*name))

#define FCK_NEXCLUDES(name, name_len) \
	{ \
		ret = cuckoo_filter_contains(filter, name, name_len); \
		ck_assert_int_eq(ret, CUCKOO_FILTER_NOT_FOUND); \
	}
#define FCK_EXCLUDES(name) FCK_NEXCLUDES(name, strlen(name))
#define FCK_PEXCLUDES(name) FCK_NEXCLUDES(name, sizeof(*name))


START_TEST(test_cuckoofilter_create)
{
	cuckoo_filter_t *filter;
	CUCKOO_FILTER_RETURN ret;
	ret = cuckoo_filter_new(&filter, 500000, 100, 0, cuckoo_filter_alloc);
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

FCK_START(test_cuckoofilter_add_more)
{
	for(int i = 0; i < 80000; i++) {
		FCK_PADD(&i);
	}
	for(int i = 0; i < 80000; i++) {
		FCK_PCONTAINS(&i);
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
		waitpid(pid, NULL, 0);
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
FCK_SHM_END

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

FCK_SHM_START(test_cuckoofilter_forked_add_multi)
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
		waitpid(pid, NULL, 0);
		// Read values child added
		for(int i = 10000; i < 80000; i++) {
			//printf("%d\n", i);
			//FCK_PCONTAINS(&i);
			ret = cuckoo_filter_contains(filter, &i, sizeof(i));
			if(ret != CUCKOO_FILTER_OK) {
				printf("\n"BYTE_TO_BINARY_PATTERN BYTE_TO_BINARY_PATTERN "\n%d\n", BYTE_TO_BINARY(i), BYTE_TO_BINARY(i >> 8), i);
			}
			ret = cuckoo_filter_add(filter, &i, sizeof(i));
			if(ret != CUCKOO_FILTER_OK) {
				printf("Failed to add\n");
			}
			ret = cuckoo_filter_contains(filter, &i, sizeof(i));
			if(ret != CUCKOO_FILTER_OK) {
				printf("Failed to contain\n");
			}
		}
	} else {
		pid = fork();
		if(pid) {
			// other child
			for(int i = 10000; i < 40000; i++) {
				ret = cuckoo_filter_add(filter, &i, sizeof(i));
				if(ret != CUCKOO_FILTER_OK) {
					printf("Error at %d\n", i);
				}
			}
			exit(0);
		}
		// Child
		// Write some values and exit
		for(int i = 40000; i < 80000; i++) {
			ret = cuckoo_filter_add(filter, &i, sizeof(i));
			if(ret != CUCKOO_FILTER_OK) {
				printf("Error at %d\n", i);
			}
		}
		waitpid(pid, NULL, 0);
		exit(0);
	}
}
FCK_SHM_END

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
		waitpid(pid, NULL, 0);
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
FCK_SHM_END

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
		waitpid(pid, NULL, 0);
		for(int i = 0; i < 10000; i++) {
			FCK_PCONTAINS(&i);
		}
	} else {
		pid = fork();
		srand(pid ? 123111 : 111321); //Set seed for master and child diffrently
		for(int i = 10000; i < 20000; i++) {
			int j = rand()%10000 + 10000;
			if(rand()%2) {
				cuckoo_filter_add(filter, &j, sizeof(j));
			} else {
				cuckoo_filter_remove(filter, &j, sizeof(j));
			}
		}
		if(pid) {
			waitpid(pid, NULL, 0);
		}
		exit(0);
	}
}
FCK_SHM_END

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
FCK_START(test_cuckoofilter_file_save_and_load)
{
	FCK_ADD("foo");
	FCK_ADD("bar");
	int fd = open("/tmp/cuckcoofiltertest", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	ck_assert_int_ge(fd, 0);
	ck_assert_int_eq(cuckoo_filter_save(filter, fd), CUCKOO_FILTER_OK);
	ck_assert_int_eq(cuckoo_filter_free(&filter), CUCKOO_FILTER_OK);
	ck_assert_int_eq(lseek(fd, 0, SEEK_SET), 0);
	ck_assert_int_eq(cuckoo_filter_load(&filter, fd, cuckoo_filter_alloc), CUCKOO_FILTER_OK);
	ck_assert_int_eq(close(fd), 0);
	FCK_CONTAINS("foo");
	FCK_CONTAINS("bar");
	FCK_PEXCLUDES("baz");
}
FCK_END

Suite *cuckoofilter_suite(void)
{
	Suite *s;
	TCase *tc_core;
	TCase *tc_shm;
	TCase *tc_file;

	s = suite_create("cuckoofilter");

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_cuckoofilter_create);
	tcase_add_test(tc_core, test_cuckoofilter_add);
	tcase_add_test(tc_core, test_cuckoofilter_remove);
	tcase_add_test(tc_core, test_cuckoofilter_add_multi);
	tcase_add_test(tc_core, test_cuckoofilter_remove_multi);
	tcase_add_test(tc_core, test_cuckoofilter_loop);
	tcase_add_test(tc_core, test_cuckoofilter_add_more);
	suite_add_tcase(s, tc_core);

	tc_shm = tcase_create("SHM");
	tcase_add_test(tc_shm, test_cuckoofilter_forked_add);
	tcase_add_test(tc_shm, test_cuckoofilter_forked_remove);
	tcase_add_test(tc_shm, test_cuckoofilter_forked_remove_and_add);
	tcase_add_test(tc_shm, test_cuckoofilter_forked_add_multi);
	suite_add_tcase(s, tc_shm);

	tc_file = tcase_create("File");
	tcase_add_test(tc_shm, test_cuckoofilter_file_save_and_load);
	suite_add_tcase(s, tc_file);
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
