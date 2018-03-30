#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "hive_memory.h"
#include "lhive_buffer.h"


#define DEFAULT_BLOCK_SIZE 256

struct buffer_block {
    struct buffer_block* next;
    size_t size;
    size_t cap;
    size_t idx;
    uint8_t data[0];
};


struct buffer_state {
    size_t all_size;
    struct buffer_block* head;
    struct buffer_block* tail;
};


static struct buffer_block *
_buffer_block_new(size_t sz) {
    struct buffer_block* p = (struct buffer_block*)hive_malloc(sizeof(struct buffer_block)+sz);
    p->size = sz;
    p->cap = 0;
    p->idx = 0;
    p->next = NULL;
    return p;
}


static struct buffer_state *
_check_state(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TUSERDATA);
    struct buffer_state* ret = lua_touserdata(L, idx);
    return ret;
}

static int
_lbuffer_size(lua_State* L) {
    struct buffer_state* s = _check_state(L, 1);
    lua_pushinteger(L, s->all_size);
    return 1;
}


static int
_lbuffer_push(lua_State* L) {
    size_t len;
    struct buffer_state* s = _check_state(L, 1);
    const char* str = luaL_tolstring(L, 2, &len);
    struct buffer_block* bp = s->tail;

    if(len == 0) {
        return 0;
    }


    if(bp->cap < bp->size) {
        size_t n = bp->size - bp->cap;
        size_t c = (len<n)?(len):(n);
        memcpy(bp->data+bp->cap, str, c);
        len = len - c;
        str += c;
        bp->cap += c;
        s->all_size += c;
    }

    if(len == 0) {
        return 0;
    }

    size_t block_sz = (len>DEFAULT_BLOCK_SIZE)?(len):(DEFAULT_BLOCK_SIZE);
    struct buffer_block* new_bp = _buffer_block_new(block_sz);
    new_bp->cap = len;
    memcpy(new_bp->data, str, len);
    bp->next = new_bp;
    s->all_size += len;
    s->tail = new_bp;
    return 0;
}


static int
_lbuffer_pop(lua_State* L) {
    struct buffer_state* s = _check_state(L, 1);
    size_t all_size = s->all_size;
    int n = luaL_optnumber(L, 2, all_size);
    int raw_n = n;

    if(n <=0 || all_size == 0) {
        lua_pushboolean(L, 0);
    }else if(n > all_size) {
        lua_pushboolean(L, 0);
    }else {
        assert(n <= all_size);
        struct buffer_block* p = s->head;
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        while(p) {
            size_t sz = p->cap - p->idx;
            struct buffer_block* next = p->next;
            if(n<sz) {
                luaL_addlstring(&b, (const char*)(p->data+p->idx), n);
                p->idx += n;
                break;
            }else {
                luaL_addlstring(&b, (const char*)(p->data+p->idx), sz);
                n -= sz;
                if(n <= 0) {
                    if(next == NULL) {
                        p->idx = 0;
                        p->cap = 0;
                    } else {
                        hive_free(p);
                        p = next;
                    }
                    break;
                }
                hive_free(p);
            }
            p = next;
        }
        s->head = p;
        s->all_size -= raw_n;
        luaL_pushresult(&b);
    }
    return 1;
}


/*
static int
_lbuffer_dump(lua_State* L) {
    struct buffer_state* s = _check_state(L, 1);
    struct buffer_block* p = s->head;
    printf("------ buffer:%p -----\n",s);
    printf("all_size:%lu tail:%p head:%p\n", s->all_size, s->tail, s->head);
    while(p) {
        printf("size:%lu cap:%lu idx:%lu next:%p\n", p->size, p->cap, p->idx, p->next);
        p = p->next;
    }
    return 0;
}
*/

static int
_lbuffer_gc(lua_State* L) {
    struct buffer_state* s = _check_state(L, 1);
    struct buffer_block* p = s->head;
    while(p) {
        struct buffer_block* next = p->next;
        hive_free(p);
        p = next;
    }
    return 0;
}

static int
_lhive_buffer_create(lua_State* L) {
    struct buffer_state* p = (struct buffer_state*)lua_newuserdata(L, sizeof(struct buffer_state));
    struct buffer_block* bp = _buffer_block_new(DEFAULT_BLOCK_SIZE);
    p->tail = bp;
    p->head = bp;
    p->all_size = 0;
    if(luaL_newmetatable(L, "HIVE_BUFFER_MT")) {
        luaL_Reg l[] = {
            {"pop", _lbuffer_pop},
            {"size", _lbuffer_size},
            {"push", _lbuffer_push},
            // {"dump", _lbuffer_dump},
            {NULL, NULL},
        };
        luaL_newlib(L, l);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, _lbuffer_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}


int
lhive_luaopen_buffer(lua_State* L) {
    luaL_Reg l[] = {
        {"create", _lhive_buffer_create},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    return 1;
}
