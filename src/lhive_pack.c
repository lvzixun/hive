#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#include "hive_memory.h"
#include "lhive_pack.h"

#define DEFAULT_STREAM_SIZE 64

enum pack_type {
    PT_NIL,
    PT_BOOLEAN,
    PT_INTEGER,
    PT_REAL,
    PT_STRING,
    PT_TABLE,
};


struct pack_stream {
    uint8_t constant[DEFAULT_STREAM_SIZE];
    uint8_t* buffer;
    size_t len;
    size_t size;
};


static size_t _lpack_table(lua_State* L, struct pack_stream* stream, int tb_idx, int rc_idx);
static void _lpack_value(lua_State* L, struct pack_stream* stream, int value_idx, int rc_idx);


static void
_stream_init(struct pack_stream* stream) {
    stream->buffer = stream->constant;
    stream->len = 0;
    stream->size = DEFAULT_STREAM_SIZE;
}


static void
_stream_free(struct pack_stream* stream) {
    if(stream->buffer != stream->constant) {
        hive_free(stream->buffer);
    }
}



static void
_stream_expand(struct pack_stream* stream) {
    stream->size *= 2;
    if(stream->buffer == stream->constant) {
        stream->buffer = (uint8_t*)hive_malloc(new_sz);
        memcpy(stream->buffer, stream->constant, stream->len);
    }else {
        stream->buffer = (uint8_t*)hive_realloc(stream->size);
        assert(stream->buffer);
    }
}

static size_t
_stream_pos(struct pack_stream* stream) {
    return stream->len;
}

static void
_stream_rewrite(struct pack_stream* stream, size_t pos, uint8_t* data, size_t sz) {
    assert(pos+sz <= stream->len);
    memcpy(stream->buffer+pos, data, sz);
}


static void
_stream_push_data(struct pack_stream* stream, uint8_t* data, size_t sz) {
    size_t cap = stream->size - stream->len;
    if(cap<sz) {
        _stream_expand(stream->buffer);
    }
    assert(stream->size - stream->len >= sz);
    memcpy(stream->buffer+stream->len, data, sz);
    stream->len += sz;
}

static void
_stream_push_type(struct pack_stream* stream, enum pack_type t) {
    uint8_t type = (uint8_t)t;
    _stream_push_data(stream, &type, sizeof(type));
}

static void
_stream_push_integer(struct pack_stream* stream, lua_Integer v) {
    _stream_push_type(stream, PT_INTEGER);
    _stream_push_data(stream, (uint8_t*)&v, sizeof(v));
}


static void
_stream_push_real(struct pack_stream* stream, lua_Number v) {
    _stream_push_type(stream, PT_REAL);
    _stream_push_data(stream, (uint8_t*)&v, sizeof(v));
}


static void
_stream_push_nil(struct pack_stream* stream) {
    _stream_push_type(stream, PT_NIL);
    _stream_push_data(stream, (uint8_t*)&type, sizeof(type));
}


static void
_stream_push_boolean(struct pack_stream* stream, bool v) {
    _stream_push_type(stream, PT_BOOLEAN);
    _stream_push_data(stream, (uint8_t*)&v, sizeof(v));
}


static void
_stream_push_string(struct pack_stream* stream, const char* s, size_t sz) {
    _stream_push_type(stream, PT_STRING);
    _stream_push_data(stream, (uint8_t*)&sz, sizeof(sz));
    _stream_push_data(stream, (uint8_t*)s, sz);
}

static void
_lpack_value(lua_State* L, struct pack_stream* stream, int value_idx, int rc_idx) {
    int type = lua_type(L, value_idx);
    switch(type) {
        case LUA_TNIL: {
            _stream_push_nil(stream);
        }
        break;

        case LUA_TBOOLEAN: {
            bool v = lua_toboolean(L, value_idx);
            _stream_push_boolean(stream, v);
        }
        break;

        case LUA_TSTRING: {
            size_t sz;
            cosnt char* s = lua_tolstring(L, value_idx, &sz);
            _stream_push_string(stream, s, sz);
        }
        break;

        case LUA_TNUMBER: {
            if(lua_isinteger(L, value_idx)) {
                lua_Integer v = lua_tointeger(L, value_idx);
                _stream_push_integer(stream, v);
            }else {
                lua_Number v = lua_tonumber(L, value_idx);
                _stream_push_real(stream, v);
            }
        }
        break;

        case LUA_TTABLE: {
            _stream_push_type(stream, PT_TABLE);
            size_t cur_pos = _stream_pos(stream);
            _stream_push_data(stream, (uint8_t*)&cur_pos, sizeof(pos));
            size_t data_pos = _lpack_table(L, stream, value_idx, rc_idx);
            _stream_rewrite(stream, cur_pos, (uint8_t*)&data_pos, sizeof(data_pos));
        }
        break;

        default: {
            const char* type_name = lua_typename(L, value_idx);
            luaL_error(L, "invalid pack lua type:%s", type_name)
        }
        break;
    }
}


static size_t
_lpack_table(lua_State* L, struct pack_stream* stream, int tb_idx, int rc_idx) {
    int top = lua_gettop(L);
    size_t cur = 0;

    lua_checkstack(L, 3);
    lua_pushvalue(L, tb_idx);
    lua_gettable(L, rc_idx);
    if(lua_isnil(L, -1)) {
        cur = _stream_pos(stream);
        uint32_t fn = 0;
        _stream_push_data(stream, (uint8_t*)&fn, sizeof(fn));
        lua_pop(L, 1);
        lua_pushvalue(L, tb_idx);
        lua_pushinteger(L, cur);
        lua_settable(L, rc_idx);
        lua_pushnil(L);
        while(lua_next(L, tb_idx) != 0) {
            int value_idx = lua_gettop(L);
            int key_idx = value_idx-1;
            _lpack_value(L, stream, key_idx, rc_idx);
            _lpack_value(L, stream, value_idx, rc_idx);
            fn++;
            if(fn == 0) {
                break;  // table is to big!
            }
        }
        _stream_rewrite(data_stream, cur, (uint8_t*)&fn, sizeof(fn));
    }else {
        cur = lua_tointeger(L, -1);    
    }
    lua_settop(L, top);
    return cur;
}


static int
_lstream_free(lua_State* L) {
    struct pack_stream* stream = (struct pack_stream*)lua_touserdata(L, -1);
    _stream_free(stream);
    return 0;
}

static struct pack_stream*
_lstream_new(lua_State* L) {
    struct pack_stream* stream = (struct pack_stream*)lua_newuserdata(L, sizeof(stream));
    _stream_init(stream);
    if(luaL_newmetatable(L, "HIVE_PACK_STREAM_METATABLE")) {
        lua_pushcfunction(L, _lstream_free);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return stream;
}


static int
_lpack(lua_State* L) {
    int top = lua_gettop(L);
    if(top<=0) {
        return 0;
    }

    lua_checkstack(L, 2);
    struct pack_stream* stream = _lstream_new(L);
    lua_newtable(L);
    int rc_idx = lua_gettop(L);
    int i;
    for(i=1; i<=top; i++) {
        _lpack_value(L, stream, i, rc_idx);
    }

    lua_pushlstring(L, (const char*)stream->buffer, stream->len);
    return 1;
}



static int
_lunpack(lua_State* L) {
    return 0;
}



int 
lhive_luaopen_pack(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"pack", _lpack},
        {"unpack", _lunpack},
        {NULL, NULL},
    }

    luaL_newlib(L, l);
    return 1;
}

