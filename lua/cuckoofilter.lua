local ffi = require("ffi")

ffi.cdef[[
typedef enum {
	CUCKOO_FILTER_OK = 0,
	CUCKOO_FILTER_NOT_FOUND = 1,
	CUCKOO_FILTER_FULL = 2,
	CUCKOO_FILTER_RETRY = 3,
	CUCKOO_FILTER_ERROR = 4
} CUCKOO_FILTER_RETURN;

typedef struct cuckoo_filter_t cuckoo_filter_t;

typedef CUCKOO_FILTER_RETURN (*cuckoo_allocate)(cuckoo_filter_t **filter,
						size_t size);
typedef CUCKOO_FILTER_RETURN (*cuckoo_deallocate)(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN
cuckoo_filter_new(cuckoo_filter_t **filter, uint64_t max_key_count,
		  uint64_t max_kick_attempts, uint32_t seed,
		  cuckoo_allocate allocator);

CUCKOO_FILTER_RETURN
cuckoo_filter_load(cuckoo_filter_t **filter, int fd, cuckoo_allocate allocator);

CUCKOO_FILTER_RETURN
cuckoo_filter_save(cuckoo_filter_t *filter, int fd);

CUCKOO_FILTER_RETURN
cuckoo_filter_add(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove(cuckoo_filter_t *filter, const void *key, size_t keylen);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains(cuckoo_filter_t *filter, const void *key, size_t keylen);

// Allocators
// SHM
CUCKOO_FILTER_RETURN cuckoo_filter_shm_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_shm_alloc(cuckoo_filter_t **filter,
					     size_t size);
// Single process
CUCKOO_FILTER_RETURN cuckoo_filter_free(cuckoo_filter_t **filter);

CUCKOO_FILTER_RETURN cuckoo_filter_alloc(cuckoo_filter_t **filter, size_t size);
]]

local C = ffi.load("libcuckoofilter.so")

local cuckoo_filter_t = ffi.typeof("cuckoo_filter_t *[1]")

local string_check = function(filter, func, element)
    if type(element) ~= "string" then
        ngx.log(ngx.ERR, type(element));
        return nil, "element must be string type"
    end

    return func(filter, element, #element) == 0
end

return function(size, depth, seed)
    if type(size) ~= "number" or size < 0 then
        return nil, "size must be a positive number"
    end

    if depth ~= nil and (type(depth) ~= "number" or depth < 0) then
        return nil, "depth must be a positive number"
    end

    local filter = cuckoo_filter_t()
    if C.cuckoo_filter_new(filter, size, depth or 100, seed or 0,
        C.cuckoo_filter_shm_alloc) ~= 0 then
        return nil, "failed to initialize filter"
    end

    ffi.gc(filter, C.cuckoo_filter_shm_free)

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
