# Lock less Cuckoo Filter Library

Similar to a Bloom filter, a Cuckoo filter provides a space-efficient data structure designed to answer approximate set-membership queries (e.g. "is item x contained in this set?") Unlike standard Bloom filters, however, Cuckoo filters support deletion. Likewise, Cuckoo filters are more optimal than Bloom variants which support deletion, such as counting Bloom filters, in both space and time.

Cuckoo filters are based on cuckoo hashing. A Cuckoo filter is essentially a cuckoo hash table which stores each key's fingerprint. As Cuckoo hash tables are highly compact, a cuckoo filter often requires less space than conventional Bloom filters for applications that require low false positive rates (< 3%).

## Implementation Details
This library was designed to provide a target false positive probability of ~P(0.001) and was hard-coded to use sixteen bits per item and four nests per bucket. As it uses two hashes, it's a (2, 4)-cuckoo filter.

## Interface
A Cuckoo filter supports following operations:

```c
CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed,
		  cuckoo_allocate allocator);
CUCKOO_FILTER_RETURN
cuckoo_filter_load(cuckoo_filter_t **filter, int fd, cuckoo_allocate allocator);

CUCKOO_FILTER_RETURN
cuckoo_filter_save(cuckoo_filter_t *filter, int fd);
```
creates, load or save filter.

```c
// Allocators
// SHM
CUCKOO_FILTER_RETURN cuckoo_filter_shm_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_shm_alloc(cuckoo_filter_t **filter,
					     size_t size);
// Single process
CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_alloc(cuckoo_filter_t **filter, size_t size);
```
free filter, remember to always use the same allocator and free function.
```c
CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const void *key, size_t keylen);
```
add, remove and test membership in order

## Usage
### C Usage
```c
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
```

More c usage exsamples in exsample/

### Lua Usage
Openresty
```nginx
worker_processes  12;
events{}

http {
    init_by_lua_block {
        local cuckoo, err = require("cuckoofilter")("ckf_1",1000000, 100, 0)
        if not cuckoo then
            return ngx.log(ngx.ERR, "INIT_ERR: ", err)
        end
	_G.cuckoo = cuckoo
    }

    server {
        listen 80;
        location / {
            content_by_lua_block {
                if ngx.var.request_method == "POST" then
                    local ok, err = cuckoo.add(ngx.var.uri)
                    return ngx.say(ok and "OK" or err)
                end
                return cuckoo.contains(ngx.var.uri) and ngx.say("HIT") or ngx.exit(404)
            }
        }
    }
}
```




## Repository structure

``example/``: some simple exsample uses

``lua/``: luajit FFI bindings

``include/``: the public header file

``src/cuckoo_filter.c``: a C-based implementation of a (2, 4)-cuckoo filter

``tests/``: unit tests, and benschmarking tools

## Installation and testing
### Install
``` sh
make
make install
```

### Run tests:
```
make run_tests
```
### Run benchmark
```
make benchmark
```
## Authors
Jonah H. Harris <jonah.harris@gmail.com>

Odin Hultgren Van Der Horst <odin@digitalgarden.no>

## License
Work by Jonah H. Harris licensed under The MIT License.

Changes licensed under AGPL

## References
* "Cuckoo Filter: Better Than Bloom" by Bin Fan, Dave Andersen, and Michael Kaminsky

