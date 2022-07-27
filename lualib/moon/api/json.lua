error("DO NOT REQUIRE THIS FILE")

---@class json
local json = {}

---@param t table
---@param empty_as_array? boolean @default true
---@param format? boolean @default true, pretty
---@return string
function json.encode(t, empty_as_array, format)

end

---@param t table
---@return string
function json.pretty_encode(t)

end

---@param str string
---@return table
---@overload fun(p:cstring_ptr, n:integer):table
function json.decode(str)

end

--- For concat sql string
---@param array any[]
---@return buffer_ptr
---@overload fun(str:string):buffer_ptr
function json.concat(array)

end

--- concat as redis protocol
---@return buffer_ptr
function json.concat_resp(...)

end

return json