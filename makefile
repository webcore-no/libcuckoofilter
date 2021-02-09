CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -fsanitize=address -O2 -g3 -I include -fPIC -lpthread -pthread -DRWLOCK
SOURCE := $(wildcard src/*.c)
OBJECTS := $(SOURCE:src/%.c=build/%.o)

all: test test2 test_pthread build/libcuckoofilter.so build/libcuckoofilter.a

test: $(OBJECTS) tests/cuckootest.c
	$(CC) $(CFLAGS) $^ -o $@

test2: $(OBJECTS) tests/cuckootest2.c
	$(CC) $(CFLAGS) $^ -o $@

test_pthread: $(OBJECTS) tests/cuckootest_pthread.c
	$(CC) $(CFLAGS) $^ -o $@

build/libcuckoofilter.so: $(OBJECTS)
	$(CC) $(CFLAGS) -shared $^ -o $@

build/libcuckoofilter.a: $(OBJECTS)
	ar rs $@ $^

build:
	mkdir build

clean:
	rm -r build test test2

build/%.o: src/%.c build
	$(CC) $(CFLAGS) -c $< -o $@
