local ffi = require("ffi")

ffi.cdef[[
typedef enum {
	CUCKOO_FILTER_OK = 0,
	CUCKOO_FILTER_NOT_FOUND,
	CUCKOO_FILTER_FULL,
	CUCKOO_FILTER_ALLOCATION_FAILED,
	CUCKOO_FILTER_RETRY
} CUCKOO_FILTER_RETURN;

typedef struct cuckoo_filter_t cuckoo_filter_t;

CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed);

CUCKOO_FILTER_RETURN
cuckoo_filter_shm_new(const char *name, cuckoo_filter_t **filter,
		      uint64_t max_key_count, size_t max_kick_attempts,
		      uint32_t seed);

CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const uint8_t *key,
		  uint64_t key_length_in_bytes);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const uint8_t *key,
		     uint64_t key_length_in_bytes);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const uint8_t *key,
		       uint64_t key_length_in_bytes);
]]

local C = ffi.load("../build/libcuckoofilter.so")

local cuckoo_filter_t = ffi.typeof("cuckoo_filter_t *[1]")

local string_check = function(filter, func, element)
    if type(element) ~= "string" then
        ngx.log(ngx.ERR, type(element));
        return nil, "element must be string type"
    end

    return func(filter, element, #element) == 0
end

return function(name, size, depth, seed)
    if type(size) ~= "number" or size < 0 then
        return nil, "size must be a positive number"
    end
    if type(name) ~= "string" then
        return nil, "name must be a string"
    end

    if depth ~= nil and (type(depth) ~= "number" or depth < 0) then
        return nil, "depth must be a positive number"
    end

    local filter = cuckoo_filter_t()
    if C.cuckoo_filter_shm_new(name, filter, size, depth or 100, seed or 0) ~= 0 then
        return nil, "failed to initialize filter"
    end

    ffi.gc(filter, C.cuckoo_filter_free)

    return {
        add = function(element)
            return string_check(filter[0], C.cuckoo_filter_add, element)
        end,
        remove = function(element)
            return string_check(filter[0], C.cuckoo_filter_remove, element)
        end,
        contains = function(element)
            return string_check(filter[0], C.cuckoo_filter_contains, element)
        end
    }
end
