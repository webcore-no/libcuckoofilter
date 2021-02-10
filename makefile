FINGERPRINT_SIZE=7
NESTS_PER_BUCKET=2

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99  -O2 -g -I include -fPIC -lpthread -pthread -DCUCKOO_FINGERPRINT_SIZE=$(FINGERPRINT_SIZE) -DCUCKOO_NESTS_PER_BUCKET=$(NESTS_PER_BUCKET)


SOURCE := $(wildcard src/*.c)
OBJECTS := $(SOURCE:src/%.c=build/%.o)

TSOURCE := $(wildcard tests/*.c)
TESTS := $(TSOURCE:%.c=%)

all: $(TESTS) build/libcuckoofilter.so build/libcuckoofilter.a

tests/%: $(OBJECTS) tests/%.c
	$(CC) $(CFLAGS) $^ -o $@

build/libcuckoofilter.so: $(OBJECTS)
	$(CC) $(CFLAGS) -shared $^ -o $@

build/libcuckoofilter.a: $(OBJECTS)
	ar rs $@ $^

build:
	mkdir build

clean:
	rm -r build $(TESTS)

build/%.o: src/%.c build
	$(CC) $(CFLAGS) -c $< -o $@


ru_domains.gz:
	wget https://partner.r01.ru/zones/ru_domains.gz
fullness: ru_domains.gz
	zcat ru_domains.gz|awk '{print $1}'|tr 'A-Z' 'a-z'
