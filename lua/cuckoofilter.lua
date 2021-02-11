local ffi = require("ffi")
ffi.cdef[[
typedef enum {
  CUCKOO_FILTER_OK = 0,
  CUCKOO_FILTER_NOT_FOUND,
  CUCKOO_FILTER_FULL,
  CUCKOO_FILTER_ALLOCATION_FAILED,
  CUCKOO_FILTER_BUSY,
  CUCKOO_FILTER_RETRY,
} CUCKOO_FILTER_RETURN;


typedef struct cuckoo_filter_t cuckoo_filter_t;

CUCKOO_FILTER_RETURN
cuckoo_filter_new (
  cuckoo_filter_t     **filter,
  size_t                max_key_count,
  size_t                max_kick_attempts,
  uint32_t              seed
);

CUCKOO_FILTER_RETURN
cuckoo_filter_free (
  cuckoo_filter_t     **filter
);

CUCKOO_FILTER_RETURN
cuckoo_filter_add (
  cuckoo_filter_t      *filter,
  const char                 *key,
  size_t                key_length_in_bytes
);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove (
  cuckoo_filter_t      *filter,
  const char                 *key,
  size_t                key_length_in_bytes
);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains (
  cuckoo_filter_t      *filter,
  const char                 *key,
  size_t                key_length_in_bytes
);
const char *
cuckoo_strerr (CUCKOO_FILTER_RETURN);
]]
local cuckoo = ffi.load("libcuckoofilter")

local _M = {}
local _MT = {__index=_M}

function _M.new(size, depth, seed)
                local filter = ffi.new("cuckoo_filter_t *[1]", cuckoo)
                local err = cuckoo.cuckoo_filter_new(filter, size, depth, seed or 0);
                if tonumber(err) ~= 0 then
                                return nil, "Failed to make filter"
                end
                local tbl = {
                                filter = filter
                }
                setmetatable(tbl, _MT)
                return tbl
end

function _M:add(element)
               if tonumber(cuckoo.cuckoo_filter_add(self.filter[0], element, #element)) == 0 then
                                return true
                end
                return nil, "Failed to add element"
end

function _M:remove(element)
                if tonumber(cuckoo.cuckoo_filter_remove(self.filter[0], element, #element)) == 0 then
                                return true
                end
                return nil, "Failed to remove element"
end

function _M:contains(element)
                return tonumber(cuckoo.cuckoo_filter_contains(self.filter[0], element, #element)) == 0
end
return _M
