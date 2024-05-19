#include "lua.hpp"
#include "config.hpp"
#include "common/buffer.hpp"
#include "common/byte_convert.hpp"

using namespace moon;

constexpr int MAX_DEPTH = 32;

static buffer* get_pointer(lua_State* L, int index) {
    buffer* b = nullptr;
    if (lua_type(L, index) == LUA_TLIGHTUSERDATA) {
        b = static_cast<buffer*>(lua_touserdata(L, index));
    }
    else {
        auto shr = static_cast<buffer_shr_ptr_t*>(lua_touserdata(L, index));
        if (shr == nullptr) {
            luaL_argerror(L, index, "null buffer_shr_ptr_t pointer");
            return nullptr;
        }
        b = shr->get();
    }

    if (b == nullptr)
        luaL_argerror(L, index, "null buffer pointer");
    return b;
}

static int clear(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    buf->clear();
    return 0;
}

static int size(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    lua_pushinteger(L, buf->size());
    return 1;
}

template<typename T>
static void pushinteger(lua_State* L, const char*& b, const char* e, bool little) {
    if ((size_t)(e-b) < sizeof(T))
        luaL_error(L, "data string too short");
    T v = 0;
    memcpy(&v, b, sizeof(T));
    b += sizeof(T);
    if (!little)
        moon::host2net(v);
    lua_pushinteger(L, v);
}

static int unpack(lua_State* L)
{
    auto buf = get_pointer(L, 1);

    int top = lua_gettop(L);

    int tp = lua_type(L, 2);
    if (tp == LUA_TSTRING)
    {
        size_t opt_len = 0;
        const char* opt = luaL_optlstring(L, 2, "", &opt_len);
        auto pos = static_cast<size_t>(luaL_optinteger(L, 3, 0));
        if (pos > buf->size())
            return luaL_argerror(L, 3, "out of range");

        const char* start = buf->data() + pos;
        const char* end = buf->data() + buf->size();

        bool little = true;
        for (size_t i = 0; i < opt_len; ++i) {
            switch (opt[i])
            {
            case '>':
                little = false;
                break;
            case '<':
                little = true;
                break;
            case 'h':
                pushinteger<int16_t>(L, start, end, little);
                break;
            case 'H':
                pushinteger<uint16_t>(L, start, end, little);
                break;
            case 'i':
                pushinteger<int32_t>(L, start, end, little);
                break;
            case 'I':
                pushinteger<uint32_t>(L, start, end, little);
                break;
            case 'C':
                lua_pushlightuserdata(L, (void*)start);
                lua_pushinteger(L, end - start);
                break;
            default:
                return luaL_error(L, "invalid format option '%c'", opt[i]);
            }
        }
    }
    else
    {
        auto pos = static_cast<size_t>(luaL_optinteger(L, 2, 0));
        if (pos > buf->size())
            return luaL_argerror(L, 2, "out of range");
        auto count = static_cast<size_t>(luaL_optinteger(L, 3, -1));
        count = std::min(buf->size() - pos, count);
        lua_pushlstring(L, buf->data() + pos, count);
    }
    return lua_gettop(L) - top;
}

static int read(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    auto count = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (count > buf->size())
        return luaL_argerror(L, 2, "out of range");
    lua_pushlstring(L, buf->data(), count);
    buf->consume(count);
    return 1;
}

static void concat_one(lua_State *L, buffer* b, int index, int depth);

static int concat_table_array(lua_State *L, buffer* buf, int index, int depth) {
    int array_size = (int)lua_rawlen(L, index);
    int i;
    for (i = 1; i <= array_size; i++) {
        lua_rawgeti(L, index, i);
        concat_one(L, buf, -1, depth);
        lua_pop(L, 1);
    }
    return array_size;
}

static void concat_table(lua_State*L, buffer* buf, int index, int depth)
{
    luaL_checkstack(L, LUA_MINSTACK, NULL);
    if (index < 0) {
        index = lua_gettop(L) + index + 1;
    }
    concat_table_array(L, buf, index, depth);
}

static void concat_one(lua_State *L, buffer* b, int index, int depth)
{
    if (depth > MAX_DEPTH) {
        throw std::logic_error{"buffer.concat too depth table"};
    }

    int type = lua_type(L, index);
    switch (type) {
    case LUA_TNIL:
        break;
    case LUA_TNUMBER:
    {
        if (lua_isinteger(L, index))
            b->write_chars(lua_tointeger(L, index));
        else
            b->write_chars(lua_tonumber(L, index));
        break;
    }
    case LUA_TBOOLEAN:
    {
        int n = lua_toboolean(L, index);
        std::string_view s = n ? "true" : "false";
        b->write_back(s.data(), s.size());
        break;
    }
    case LUA_TSTRING: {
        size_t sz = 0;
        const char *str = lua_tolstring(L, index, &sz);
        b->write_back(str, sz);
        break;
    }
    case LUA_TTABLE: {
        if (index < 0)
        {
            index = lua_gettop(L) + index + 1;
        }
        concat_table(L, b, index, depth + 1);
        break;
    }
    default:
        throw std::logic_error{std::string("buffer.concat_one unsupport type :")+lua_typename(L, type)};
    }
}

static int write_front(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    int top = lua_gettop(L);
    bool ok = true;
    for(int i = top; i>1; --i){
        size_t len = 0;
        auto data = luaL_checklstring(L, i, &len);
        ok = buf->write_front(data, len);
        if(!ok) break;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int write_back(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    try
    {
        int n = lua_gettop(L);
        for (int i = 2; i <= n; i++) {
            concat_one(L, buf, i, 0);
        }
        return 0;
    }
    catch(const std::exception& e)
    {
        lua_pushstring(L, e.what());
    }
    return lua_error(L);
}

static int seek(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    auto pos = static_cast<size_t>(luaL_checkinteger(L, 2));
    auto origin = ((luaL_optinteger(L, 3, 1) == 1) ? buffer::seek_origin::Current : buffer::seek_origin::Begin);
    if (!buf->seek(pos, origin))
        return luaL_error(L, "out off range");
    return 0;
}

static int commit(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    auto n = static_cast<size_t>(luaL_checkinteger(L, 2));
    buf->commit(n);
    return 0;
}

static int prepare(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    auto n = static_cast<size_t>(luaL_checkinteger(L, 2));
    if (0 == n)
    {
        return luaL_error(L, "Invalid buffer prepare param");
    }
    buf->prepare(n);
    return 0;
}

static int unsafe_delete(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    delete buf;
    return 0;
}

static int unsafe_new(lua_State* L)
{
    size_t capacity = static_cast<size_t>(luaL_optinteger(L, 1, buffer::DEFAULT_CAPACITY));
    buffer* buf = new buffer{capacity};
    lua_pushlightuserdata(L, buf);
    return 1;
}

static int concat(lua_State* L)
{
    int n = lua_gettop(L);
    if (0 == n)
    {
        return 0;
    }
    auto buf = new buffer{};
    buf->commit(BUFFER_OPTION_CHEAP_PREPEND);
    try
    {
        for (int i = 1; i <= n; i++) {
            concat_one(L, buf, i, 0);
        }
        buf->seek(BUFFER_OPTION_CHEAP_PREPEND);
        lua_pushlightuserdata(L, buf);
        return 1;
    }
    catch(const std::exception& e)
    {
        delete buf;
        lua_pushstring(L, e.what());
    }
    return lua_error(L);
}

static int concat_string(lua_State *L)
{
    int n = lua_gettop(L);
    if (0 == n)
        return 0;

    try
    {
        buffer buf;
        for (int i = 1; i <= n; i++) {
            concat_one(L, &buf, i, 0);
        }
        lua_pushlstring(L, buf.data(), buf.size());
        return 1;
    }
    catch(const std::exception& e)
    {
        lua_pushstring(L, e.what());
    }
    return lua_error(L);
}

static int to_shared(lua_State* L)
{
    buffer* b = (buffer*)lua_touserdata(L, 1);
    if (nullptr == b)
        return luaL_argerror(L, 1, "lightuserdata(buffer*) expected");

    if (b->size() == 0) {
        return 0;
    }

    void* space = lua_newuserdatauv(L, sizeof(moon::buffer_shr_ptr_t), 0);
    new (space) moon::buffer_shr_ptr_t{ b };
    if (luaL_newmetatable(L, "lbuffer_shr_ptr"))//mt
    {
        auto gc = [](lua_State* L)
            {
                buffer_shr_ptr_t* shr = (buffer_shr_ptr_t*)lua_touserdata(L, 1);
                if (nullptr == shr)
                    return luaL_argerror(L, 1, "invalid buffer_shr_ptr_t pointer");
                std::destroy_at(shr);
                return 0;
            };
        lua_pushcclosure(L, gc, 0);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

static int has_bitmask(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    bool has = buf->has_bitmask(static_cast<socket_send_mask>(luaL_checkinteger(L, 2)));
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

static int add_bitmask(lua_State* L)
{
    auto buf = get_pointer(L, 1);
    auto bitmask = static_cast<socket_send_mask>(luaL_checkinteger(L, 2));
    buf->add_bitmask(bitmask);
    return 0;
}

extern "C" {
    int LUAMOD_API luaopen_buffer(lua_State* L)
    {
        luaL_Reg l[] = {
            {"unsafe_new", unsafe_new}
            , {"delete", unsafe_delete}
            , {"clear", clear}
            , {"size", size}
            , {"unpack", unpack}
            , {"read", read}
            , {"write_front", write_front}
            , {"write_back", write_back}
            , {"seek", seek}
            , {"commit", commit}
            , {"prepare", prepare}
            , {"concat", concat }
            , {"concat_string", concat_string }
            , {"to_shared", to_shared }
            , {"has_bitmask", has_bitmask}
            , {"add_bitmask", add_bitmask}
            , {NULL, NULL}
        };
        luaL_newlib(L, l);
        return 1;
    }
}
