# Cuckoo Filter Library

Similar to a Bloom filter, a Cuckoo filter provides a space-efficient data structure designed to answer approximate set-membership queries (e.g. "is item x contained in this set?") Unlike standard Bloom filters, however, Cuckoo filters support deletion. Likewise, Cuckoo filters are more optimal than Bloom variants which support deletion, such as counting Bloom filters, in both space and time.

Cuckoo filters are based on cuckoo hashing. A Cuckoo filter is essentially a cuckoo hash table which stores each key's fingerprint. As Cuckoo hash tables are highly compact, a cuckoo filter often requires less space than conventional Bloom filters for applications that require low false positive rates (< 3%).

## Install instructions
``` sh
make
make install
```

## Implementation Details
This library was designed to provide a target false positive probability of ~P(0.001) and was hard-coded to use sixteen bits per item and four nests per bucket. As it uses two hashes, it's a (2, 4)-cuckoo filter.

## Interface
A Cuckoo filter supports following operations:

*  ``cuckoo_filter_new(filter, max_key_count, max_kick_attempts, seed)``: creates a filter
*  ``cuckoo_filter_free(filter)``: destroys a filter
*  ``cuckoo_filter_add(filter, item, item_length_in_bytes)``: add an item to the filter, returns CUCKOO_FILTER_BUSY if somone is already changing the filter
*  ``cuckoo_filter_remove(filter, item, item_length_in_bytes)``: remove an item from the filter, returns CUCKOO_FILTER_BUSY if somone is already changing the filter
*  ``cuckoo_filter_blocking_add(filter, item, item_length_in_bytes)``: add an item to the filter
*  ``cuckoo_filter_blocking_remove(filter, item, item_length_in_bytes)``: remove an item from the filter
*  ``cuckoo_filter_contains(filter, item, item_length_in_bytes)``: test for approximate membership of an item in the filter

## Repository structure

*  ``example/``: an example demonstrating the use of the filter
* ``lua/``: luajit FFI bindings
*  ``include/``: the public header file
*  ``src/cuckoo_filter.c``: a C-based implementation of a (2, 4)-cuckoo filter
*  ``tests/``: unit tests

## Usage
To build this example:

    $ make test


## Authors
Jonah H. Harris <jonah.harris@gmail.com>

Odin Hultgren Van Der Horst <odin@digitalgarden.no>

## License
Work by Jonah H. Harris licensed under The MIT License.

Changes licensed under AGPL

## References
* "Cuckoo Filter: Better Than Bloom" by Bin Fan, Dave Andersen, and Michael Kaminsky

