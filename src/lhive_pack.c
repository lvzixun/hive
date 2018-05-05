#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

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
    char error[128];
};


struct pack_reader {
    uint8_t* buffer;
    size_t size;
    size_t pos;
};



static bool _lpack_table(lua_State* L, struct pack_stream* stream, int tb_idx, int rc_idx);
static bool _lpack_value(lua_State* L, struct pack_stream* stream, int value_idx, int rc_idx);

#define check_reader(reader, cap) do{ \
    if( (reader)->pos+(cap) > (reader)->size ) {\
        return false; \
    } \
}while(0)

#define preader(L, f, ...) do { \
    bool b = f(__VA_ARGS__); \
    if(!b) { \
        luaL_error(L, "invalid pack data."); \
    } \
}while(0)

#define cast_reader(reader, t) *((t*)((reader)->buffer + (reader)->pos))


static void
_reader_init(struct pack_reader* reader, const uint8_t* data, size_t sz) {
    reader->size = sz;
    reader->pos = 0;
    reader->buffer = (uint8_t*)data;
}

static bool
_reader_isend(struct pack_reader* reader) {
    return reader->pos >= reader->size;
}


static bool
_reader_type(struct pack_reader* reader, enum pack_type* out_type) {
    check_reader(reader, sizeof(uint8_t));
    uint8_t v = cast_reader(reader, uint8_t);
    *out_type = (enum pack_type)v;
    reader->pos += sizeof(uint8_t);
    return true;
}

static bool
_reader_integer(struct pack_reader* reader, lua_Integer* out_v) {
    check_reader(reader, sizeof(lua_Integer));
    *out_v = cast_reader(reader, lua_Integer);
    reader->pos += sizeof(lua_Integer);
    return true;
}

static bool
_reader_real(struct pack_reader* reader, lua_Number* out_v) {
    check_reader(reader, sizeof(lua_Number));
    *out_v = cast_reader(reader, lua_Number);
    reader->pos += sizeof(lua_Number);
    return true;
}


static bool
_reader_boolean(struct pack_reader* reader, bool* out_v) {
    check_reader(reader, sizeof(uint8_t));
    uint8_t v = cast_reader(reader, uint8_t);
    *out_v = (bool)v;
    reader->pos += sizeof(uint8_t);
    return true;
}


static bool
_reader_string(struct pack_reader* reader, const char** out_str, size_t* len) {
    check_reader(reader, sizeof(size_t));
    *len = cast_reader(reader, size_t);
    reader->pos += sizeof(size_t);
    check_reader(reader, *len);
    *out_str = (const char*)(reader->buffer + reader->pos);
    reader->pos += (*len);
    return true;
}


static bool
_reader_table(struct pack_reader* reader, int* fn) {
    check_reader(reader, sizeof(int));
    *fn = cast_reader(reader, int);
    reader->pos += sizeof(int);
    return true;
}



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
        stream->buffer = (uint8_t*)hive_malloc(stream->size);
        memcpy(stream->buffer, stream->constant, stream->len);
    }else {
        stream->buffer = (uint8_t*)hive_realloc(stream->buffer, stream->size);
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
        _stream_expand(stream);
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

static bool
_lpack_value(lua_State* L, struct pack_stream* stream, int value_idx, int rc_idx) {
    int type = lua_type(L, value_idx);
    switch(type) {
        case LUA_TNIL: {
            _stream_push_nil(stream);
        }break;

        case LUA_TBOOLEAN: {
            bool v = lua_toboolean(L, value_idx);
            _stream_push_boolean(stream, v);
        }break;

        case LUA_TSTRING: {
            size_t sz;
            const char* s = lua_tolstring(L, value_idx, &sz);
            _stream_push_string(stream, s, sz);
        }break;

        case LUA_TNUMBER: {
            if(lua_isinteger(L, value_idx)) {
                lua_Integer v = lua_tointeger(L, value_idx);
                _stream_push_integer(stream, v);
            }else {
                lua_Number v = lua_tonumber(L, value_idx);
                _stream_push_real(stream, v);
            }
        }break;

        case LUA_TTABLE: {
            _stream_push_type(stream, PT_TABLE);
            if(rc_idx == 0) {
                lua_checkstack(L, 1);
                lua_newtable(L);
                rc_idx = lua_gettop(L);
            }
            if(!_lpack_table(L, stream, value_idx, rc_idx)) {
                return false;
            }
        }break;

        default: {
            const char* type_name = lua_typename(L, value_idx);
            snprintf(stream->error, sizeof(stream->error), "invalid pack lua type:%s", type_name);
            return false;
        }break;
    }
    return true;
}


static bool
_lpack_table(lua_State* L, struct pack_stream* stream, int tb_idx, int rc_idx) {
    int top = lua_gettop(L);
    size_t cur = 0;
    bool ret = true;

    lua_checkstack(L, 3);
    lua_pushvalue(L, tb_idx);
    lua_gettable(L, rc_idx);
    if(lua_isnil(L, -1)) {
        cur = _stream_pos(stream);
        int n=0;
        _stream_push_data(stream, (uint8_t*)&n, sizeof(n));
        lua_pop(L, 1);
        lua_pushvalue(L, tb_idx);
        lua_pushinteger(L, cur);
        lua_settable(L, rc_idx);
        lua_pushnil(L);
        while(lua_next(L, tb_idx) != 0) {
            int value_idx = lua_gettop(L);
            int key_idx = value_idx-1;
            if(!_lpack_value(L, stream, key_idx, rc_idx) || 
               !_lpack_value(L, stream, value_idx, rc_idx)) {
                ret = false;
                goto PACKTABLE_END;
            }
            lua_pop(L, 1);
            n++;
        }
        _stream_rewrite(stream, cur, (uint8_t*)&n, sizeof(n));
    }else {
        snprintf(stream->error, sizeof(stream->error), "recursion table");
        ret = false;
    }

    PACKTABLE_END:
    lua_settop(L, top);
    return ret;
}



static int
_lpack(lua_State* L) {
    int top = lua_gettop(L);
    if(top<=0) {
        return 0;
    }

    struct pack_stream stream;
    _stream_init(&stream);
    int i;
    bool b = true;
    for(i=1; i<=top; i++) {
        b = _lpack_value(L, &stream, i, 0);
        if(!b) {
            break;
        }
    }

    if(b) {
        lua_pushlstring(L, (const char*)stream.buffer, stream.len);
        _stream_free(&stream);
    }else {
        _stream_free(&stream);
        luaL_error(L, "%s", stream.error);
    }
    return 1;
}

static void
_unpack_value(lua_State* L, struct pack_reader* reader) {
    enum pack_type type = PT_NIL;
    preader(L, _reader_type, reader, &type);
    lua_checkstack(L, 1);

    switch(type) {
        case PT_NIL: {
            lua_pushnil(L);
        }break;

        case PT_BOOLEAN: {
            bool v = 0;
            preader(L, _reader_boolean, reader, &v);
            lua_pushboolean(L, v);
        }break;

        case PT_INTEGER: {
            lua_Integer v = 0;
            preader(L, _reader_integer, reader, &v);
            lua_pushinteger(L, v);
        }break;

        case PT_REAL: {
            lua_Number v = 0.0;
            preader(L, _reader_real, reader, &v);
            lua_pushnumber(L, v);
        }break;

        case PT_STRING: {
            const char* str = NULL;
            size_t sz = 0;
            preader(L, _reader_string, reader, &str, &sz);
            lua_pushlstring(L, str, sz);
        }break;

        case PT_TABLE: {
            int fn = 0;
            preader(L, _reader_table, reader, &fn);
            lua_newtable(L);
            int tb_idx = lua_gettop(L);
            int i=0;
            for(i=0; i<fn; i++) {
                _unpack_value(L, reader);
                _unpack_value(L, reader);
                lua_settable(L, tb_idx);
            }
        }break;

        default:
            luaL_error(L, "invalid type:%d", type);
    }
}


static int
_lunpack(lua_State* L) {
    size_t sz;
    const char* s = lua_tolstring(L, 1, &sz);
    struct pack_reader reader;
    int n = 0;
    _reader_init(&reader, (const uint8_t*)(s), sz);
    while(!_reader_isend(&reader)) {
        _unpack_value(L, &reader);
        n++;   
    }
    return n;
}



int 
lhive_luaopen_pack(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"pack", _lpack},
        {"unpack", _lunpack},
        {NULL, NULL},
    };

    luaL_newlib(L, l);
    return 1;
}

