NESTS_PER_BUCKET=4
PREFIX=/usr

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=gnu99 -g -Ofast -I include -fPIC -DCUCKOO_NESTS_PER_BUCKET=$(NESTS_PER_BUCKET)

SOURCE := $(wildcard src/*.c)
OBJECTS := $(SOURCE:src/%.c=build/%.o)

TSOURCE := $(wildcard tests/*.c)
TESTS := $(TSOURCE:%.c=%)

ESOURCE := $(wildcard example/*.c)
EXSAMPLES := $(ESOURCE:%.c=%)

all: build/libcuckoofilter.so build/libcuckoofilter.a tests examples

tests: $(TESTS)

examples: $(EXSAMPLES)

run_tests: tests/tests
	tests/tests

benchmark: tests/benchmark
	tests/benchmark

collision: tests/collision
	tests/collision

example/%: $(OBJECTS) example/%.c
	$(CC) $(CFLAGS) -lcheck -lrt $^ -o $@

tests/%: $(OBJECTS) tests/%.c
	$(CC) $(CFLAGS) -lcheck -lrt $^ -o $@

build/libcuckoofilter.so: $(OBJECTS)
	$(CC) $(CFLAGS) -lrt -shared $^ -o $@

build/libcuckoofilter.a: $(OBJECTS)
	ar rs $@ $^

build:
	mkdir build

clean:
	rm -rf build $(TESTS) $(EXSAMPLES)

build/%.o: src/%.c build
	$(CC) $(CFLAGS) -c $< -o $@

install: build/libcuckoofilter.so build/libcuckoofilter.a
	install build/libcuckoofilter.so $(PREFIX)/lib
	install build/libcuckoofilter.a $(PREFIX)/lib
	install include/* $(PREFIX)/include
