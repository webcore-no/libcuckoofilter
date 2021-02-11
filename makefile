FINGERPRINT_SIZE=15
NESTS_PER_BUCKET=4
PREFIX=/usr


CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99  -O2 -g -I include -fPIC -lpthread -pthread -DCUCKOO_FINGERPRINT_SIZE=$(FINGERPRINT_SIZE) -DCUCKOO_NESTS_PER_BUCKET=$(NESTS_PER_BUCKET)


SOURCE := $(wildcard src/*.c)
OBJECTS := $(SOURCE:src/%.c=build/%.o)

TSOURCE := $(wildcard tests/*.c)
TESTS := $(TSOURCE:%.c=%)

all: build/libcuckoofilter.so build/libcuckoofilter.a

tests: $(TESTS)

tests/%: $(OBJECTS) tests/%.c
	$(CC) $(CFLAGS) $^ -o $@

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
	install -s build/libcuckoofilter.so $(PREFIX)/lib
	install -s build/libcuckoofilter.a $(PREFIX)/lib
	install include/* $(PREFIX)/include
