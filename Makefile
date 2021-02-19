FINGERPRINT_SIZE=16
NESTS_PER_BUCKET=4
PREFIX=/usr

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -Ofast -I include -fPIC -lpthread -lrt -DCUCKOO_FINGERPRINT_SIZE=$(FINGERPRINT_SIZE) -DCUCKOO_NESTS_PER_BUCKET=$(NESTS_PER_BUCKET) -DCUCKOO_SHM -fno-omit-frame-pointer

CFLAGS = -Wall -Wextra -std=gnu99 -g -Ofast -I include -fPIC -lpthread -lrt -DCUCKOO_FINGERPRINT_SIZE=$(FINGERPRINT_SIZE) -DCUCKOO_NESTS_PER_BUCKET=$(NESTS_PER_BUCKET) -DCUCKOO_SHM -fno-omit-frame-pointer


SOURCE := $(wildcard src/*.c)
OBJECTS := $(SOURCE:src/%.c=build/%.o)

TSOURCE := $(wildcard tests/*.c)
TESTS := $(TSOURCE:%.c=%)

all: build/libcuckoofilter.so build/libcuckoofilter.a

tests: $(TESTS) run_tests

run_tests: tests/tests
	tests/tests

benchmark: tests/benchmark
	tests/benchmark

collision: tests/collision
	tests/collision

tests/%: $(OBJECTS) tests/%.c
	$(CC) $(CFLAGS) -lcheck $^ -o $@

build/libcuckoofilter.so: $(OBJECTS)
	$(CC) $(CFLAGS) -shared $^ -o $@

build/libcuckoofilter.a: $(OBJECTS)
	ar rs $@ $^

build:
	mkdir build

clean:
	rm -rf build $(TESTS)

build/%.o: src/%.c build
	$(CC) $(CFLAGS) -c $< -o $@

install: build/libcuckoofilter.so build/libcuckoofilter.a
	install build/libcuckoofilter.so $(PREFIX)/lib
	install build/libcuckoofilter.a $(PREFIX)/lib
	install include/* $(PREFIX)/include
