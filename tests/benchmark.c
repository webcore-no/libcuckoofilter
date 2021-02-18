#include "../include/cuckoo_filter.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#define ELEMENTS 100000000
#define TEST_DURATION 10
#define PREFILL 0.5
#define CONTAINS_P 85
#define ADD_P 10
#define REMOVE_P 5



typedef struct {
	int parent_fd;
	cuckoo_filter_t *filter;
	uint32_t op_counter;
} _globals;
_globals globals;

void worker_loop()
{
	srand(getpid());
	while(true) {
		int k = rand();
		int n = k%100;
		if(n < CONTAINS_P) {
			cuckoo_filter_contains(globals.filter,(const uint8_t *)&k, sizeof(k));
		} else if(n < (CONTAINS_P+ADD_P)) {
			cuckoo_filter_blocking_add(globals.filter,(const uint8_t *)&k, sizeof(k));
		} else {
			cuckoo_filter_blocking_remove(globals.filter,(const uint8_t *)&k, sizeof(k));
		}
		globals.op_counter++;
	}
}

void handle_sighup(int __attribute__((unused)) signal) {
	// Write total ammount off operations and exit
	ssize_t wr = write(globals.parent_fd, &globals.op_counter, sizeof(uint32_t));
	if(wr == -1) {
		printf("%d:%s\n", errno, strerror(errno));
	}
	fflush(stdout);
	close(globals.parent_fd);
	exit(0);
}


typedef struct {
	int fd;
	pid_t pid;
} worker;

int create_worker(worker *wrk)
{
	int fd[2];
	int ret;
	pid_t pid;

	ret = pipe(fd);
	if(ret == -1) {
		return -1;
	}
	pid = fork();
	if(!pid) {
		// Worker
		close(fd[0]);
		srand(time(NULL));
		globals.parent_fd = fd[1];
		worker_loop();
		exit(0);
	}
	if(pid < 0) {
		printf("ERROR[%d]:%s", errno, strerror(errno));
		close(fd[0]);
		close(fd[1]);
		return -1;
	}
	close(fd[1]);
	wrk->pid = pid;
	wrk->fd = fd[0];

	return 0;
}

#define wc 8
int main(void)
{
	if(signal(SIGHUP, &handle_sighup) == SIG_ERR) {
		printf("ERROR[%d]:%s\n", errno, strerror(errno));
		exit(1);
	}
	worker workers[wc];
	if(cuckoo_filter_shm_new("tshm", &globals.filter, ELEMENTS, 150, 123)) {
		printf("ERROR: falied to make filter");
		exit(1);
	}

	int i;
	for(i = 0; i < (ELEMENTS*PREFILL); i++) {
		CUCKOO_FILTER_RETURN ret = cuckoo_filter_blocking_add(globals.filter, (const uint8_t *)&i, sizeof(i));
		if(ret != CUCKOO_FILTER_OK) {
			printf("prefill failed\n");
			exit(1);
		}

	}
	printf("prefill done\n");
	// create all workers
	for(i = 0; i < wc; i++) {
		if(create_worker(&workers[i])) {
			cuckoo_filter_free(&globals.filter);
			printf("ERROR[%d]:%s", errno, strerror(errno));
			exit(1);
		}
	}
	// sleep some time
	sleep(TEST_DURATION);
	// Kill all workers
	for(int i = 0; i < wc; i++) {
		uint32_t worker_out = 0;
		if(kill(workers[i].pid, SIGHUP)) {
			cuckoo_filter_free(&globals.filter);
			printf("ERROR[%d]:%s", errno, strerror(errno));
			exit(1);
		}
		read(workers[i].fd, &worker_out, sizeof(uint32_t));
		globals.op_counter += worker_out;
	}
	cuckoo_filter_free(&globals.filter);
	printf("%d ops/s\n", globals.op_counter/ TEST_DURATION);
	return 0;
}
