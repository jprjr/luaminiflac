#define LUAMINIFLAC_VERSION_MAJOR 1
#define LUAMINIFLAC_VERSION_MINOR 0
#define LUAMINIFLAC_VERSION_PATCH 0
#define STR(x) #x
#define XSTR(x) STR(x)
#define LUAMINIFLAC_VERSION XSTR(LUAMINIFLAC_VERSION_MAJOR) "." XSTR(LUAMINIFLAC_VERSION_MINOR) "." XSTR(LUAMINIFLAC_VERSION_PATCH)

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#define MINIFLAC_API static
#define MINIFLAC_PRIVATE static
#include "miniflac/miniflac.h"

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(_MSC_VER)
#define LUAMINIFLAC_PUBLIC __declspec(dllexport)
#elif __GNUC__ > 4
#define LUAMINIFLAC_PUBLIC __attribute__ ((visibility ("default")))
#else
#define LUAMINIFLAC_PUBLIC
#endif


#ifdef __cplusplus
extern "C" {
#endif

LUAMINIFLAC_PUBLIC
int luaopen_miniflac(lua_State *L);

#ifdef __cplusplus
}
#endif

#define MINIFLAC_IMPLEMENTATION
#include "miniflac/miniflac.h"

/* compat funcs {{{ */
#define luaminiflac_push_const(x) lua_pushinteger(L,MINIFLAC_ ## x) ; lua_setfield(L,-2, "MINIFLAC_" #x)

#if (!defined LUA_VERSION_NUM) || LUA_VERSION_NUM == 501
#define lua_setuservalue(L,i) lua_setfenv((L),(i))
#define lua_getuservalue(L,i) lua_getfenv((L),(i))
#define lua_rawlen(L,i) lua_objlen((L),(i))
#endif

#if !defined(luaL_newlibtable) \
  && (!defined LUA_VERSION_NUM || LUA_VERSION_NUM==501)
static void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
    luaL_checkstack(L, nup+1, "too many upvalues");
    for (; l->name != NULL; l++) {  /* fill the table with given functions */
        int i;
        lua_pushlstring(L, l->name,strlen(l->name));
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(L, -(nup+1));
        lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
        lua_settable(L, -(nup + 3));
    }
    lua_pop(L, nup);  /* remove upvalues */
}

static
void luaL_setmetatable(lua_State *L, const char *str) {
    luaL_checkstack(L, 1, "not enough stack slots");
    luaL_getmetatable(L, str);
    lua_setmetatable(L, -2);
}

static
void *luaL_testudata (lua_State *L, int i, const char *tname) {
    void *p = lua_touserdata(L, i);
    luaL_checkstack(L, 2, "not enough stack slots");
    if (p == NULL || !lua_getmetatable(L, i)) {
        return NULL;
    }
    else {
        int res = 0;
        luaL_getmetatable(L, tname);
        res = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (!res) {
            p = NULL;
        }
    }
    return p;
}
#endif
/* }}} */

static const char* const luaminiflac_digits          = "0123456789";
static const char* const luaminiflac_int64_mt        = "miniflac_int64_t";
static const char* const luaminiflac_uint64_mt       = "miniflac_uint64_t";
static const char* const luaminiflac_mt     = "miniflac_t";

static const char* const luaminiflac_metadata_strs[] = {
    "streaminfo",
    "padding",
    "application",
    "seektable",
    "vorbis_comment",
    "cuesheet",
    "picture",
    "invalid",
    "unknown",
};

typedef struct luaminiflac_metamethods_s {
    const char *name;
    const char *metaname;
} luaminiflac_metamethods_t;

typedef struct luaminiflac_closures_s {
    void* f;
    lua_CFunction l;
    const char* name;
} luaminiflac_closures_t;

typedef struct luaminiflac_s {
    miniflac_t flac;
    int32_t samplebuf[8 * 65535];
    int32_t* samples[8];
    uint8_t* buffer;
    uint32_t buffer_len;
} luaminiflac_t;

typedef MINIFLAC_RESULT (*luaminiflac_uint8_func)(miniflac_t* pFlac, const uint8_t* data, uint32_t length, uint32_t* out_length, uint8_t* value);
typedef MINIFLAC_RESULT (*luaminiflac_uint16_func)(miniflac_t* pFlac, const uint8_t* data, uint32_t length, uint32_t* out_length, uint16_t* value);
typedef MINIFLAC_RESULT (*luaminiflac_uint32_func)(miniflac_t* pFlac, const uint8_t* data, uint32_t length, uint32_t* out_length, uint32_t* value);
typedef MINIFLAC_RESULT (*luaminiflac_uint64_func)(miniflac_t* pFlac, const uint8_t* data, uint32_t length, uint32_t* out_length, uint64_t* value);

typedef MINIFLAC_RESULT (*luaminiflac_str_func)(miniflac_t* pFlac, const uint8_t* data, uint32_t length, uint32_t* out_length, uint8_t* buffer, uint32_t buffer_length, uint32_t* buffer_used);

/* uint64 and int64 {{{ */
static char *
luaminiflac_uint64_to_str(uint64_t value, char buffer[21], size_t *len) {
    char *p = buffer + 20;
    *p = '\0';
    do {
        *--p = luaminiflac_digits[value % 10];
    } while (value /= 10);

    *len = 20 - (p - buffer);
    return p;
}

static char *
luaminiflac_int64_to_str(int64_t value, char buffer[22], size_t *len) {
    int sign;
    uint64_t tmp;

    if(value < 0) {
        sign = 1;
        tmp = -value;
    } else {
        sign = 0;
        tmp = value;
    }
    char *p = luaminiflac_uint64_to_str(tmp,buffer+1,len);
    if(sign) {
        *--p = '-';
        *len += 1;
    }
    return p;
}

static void
luaminiflac_pushuint64(lua_State *L, int64_t val) {
    int64_t *t = NULL;

    t = lua_newuserdata(L,sizeof(int64_t));
    if(t == NULL) {
        luaL_error(L,"out of memory");
    }
    *t = val;
    luaL_setmetatable(L,luaminiflac_int64_mt);
}

static inline int64_t
luaminiflac_toint64(lua_State *L, int idx) {
    int64_t *t = NULL;
    uint64_t *r = NULL;
    int64_t tmp = 0;
    const char *str = NULL;
    const char *s = NULL;
    char *end = NULL;

    switch(lua_type(L,idx)) {
        case LUA_TNONE: {
            return 0;
        }
        case LUA_TNIL: {
            return 0;
        }
        case LUA_TBOOLEAN: {
            return (int64_t)lua_toboolean(L,idx);
        }
        case LUA_TNUMBER: {
            return (int64_t)lua_tointeger(L,idx);
        }
        case LUA_TUSERDATA: {
            t = luaL_testudata(L,idx,luaminiflac_int64_mt);
            if(t != NULL) {
                return *t;
            }
            r = luaL_testudata(L,idx,luaminiflac_uint64_mt);
            if(r != NULL) {
                if(*r > 0x7FFFFFFFFFFFFFFF) {
                    luaL_error(L,"out of range");
                    return 0;
                }
                return (int64_t) *r;
            }
            break;
        }
        /* we'll try converting to a string */
        default: break;
    }

    str = lua_tostring(L,idx);
    if(str == NULL) {
        luaL_error(L,"invalid value");
        return 0;
    }

    s = str;
    while(*s) {
        if(isspace(*s)) {
            s++;
        } else {
            break;
        }
    }

    if(!*s) { /* empty string, or just spaces */
        luaL_error(L,"invalid string, %s has no characters",str);
        return 0;
    }

    errno = 0;
    tmp = strtoll(s,&end,10);
    if(errno) {
        luaL_error(L,"invalid integer string");
        return 0;
    }
    if(s == end) {
        luaL_error(L,"invalid integer string");
        return 0;
    }
    return tmp;
}

static inline uint64_t
luaminiflac_touint64(lua_State *L, int idx) {
    uint64_t *t = NULL;
    int64_t *r = NULL;
    uint64_t tmp  = 0;
    const char *str = NULL;
    const char *s = NULL;
    char *end = NULL;

    switch(lua_type(L,idx)) {
        case LUA_TNONE: {
            return 0;
        }
        case LUA_TNIL: {
            return 0;
        }
        case LUA_TBOOLEAN: {
            return (uint64_t)lua_toboolean(L,idx);
        }
        case LUA_TNUMBER: {
            return (uint64_t)lua_tointeger(L,idx);
        }
        case LUA_TUSERDATA: {
            t = luaL_testudata(L,idx,luaminiflac_uint64_mt);
            if(t != NULL) {
                return *t;
            }
            r = luaL_testudata(L,idx,luaminiflac_int64_mt);
            if(r != NULL) {
                if(*r < 0) {
                    luaL_error(L,"out of range");
                    return 0;
                }
                return (uint64_t)*r;
            }
            break;
        }
        /* we'll try converting to a string */
        default: break;
    }

    str = lua_tostring(L,idx);
    if(str == NULL) {
        luaL_error(L,"invalid value");
        return 0;
    }

    s = str;
    while(*s) {
        if(isspace(*s)) {
            s++;
        } else {
            break;
        }
    }

    if(!*s) { /* empty string, or just spaces */
        luaL_error(L,"invalid string, %s has no characters",str);
        return 0;
    }
    if(*s == '-') {
        luaL_error(L,"invalid string, %s is negative",str);
        return 0;
    }
    errno = 0;
    tmp = strtoull(s, &end, 10);
    if(errno) {
        luaL_error(L,"invalid integer string");
        return 0;
    }
    if(s == end) {
        luaL_error(L,"invalid string");
        return 0;
    }
    return tmp;
}


static int
luaminiflac_uint64(lua_State *L) {
    /* create a new int64 object from a number or string */
    uint64_t *t = 0;

    t = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    if(t == NULL) {
        return luaL_error(L,"out of memory");
    }

    if(lua_gettop(L) > 1) {
        *t = luaminiflac_touint64(L,1);
    }
    else {
        *t = 0;
    }

    luaL_setmetatable(L,luaminiflac_uint64_mt);
    return 1;
}


static int
luaminiflac_uint64__unm(lua_State *L) {
    uint64_t *o = NULL;
    int64_t *r = NULL;

    o = lua_touserdata(L,1);

    if(*o > 0x8000000000000000) {
        return luaL_error(L,"out of range");
    }

    r = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *r = -(*o);

    luaL_setmetatable(L,luaminiflac_int64_mt);
    return 1;
}

static int
luaminiflac_uint64__add(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a + b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__sub(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a - b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__mul(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a * b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__div(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a / b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__mod(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a % b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__pow(lua_State *L) {
    uint64_t base = 0;
    uint64_t exp = 0;
    uint64_t *res = NULL;
    uint64_t result = 1;

    base = luaminiflac_touint64(L,1);
    exp = luaminiflac_touint64(L,2);

    for (;;) {
        if(exp & 1) {
            result *= base;
        }
        exp >>= 1;
        if(!exp) {
            break;
        }
        base *= base;
    }

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = result;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__eq(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    lua_pushboolean(L,a==b);
    return 1;
}

static int
luaminiflac_uint64__lt(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    lua_pushboolean(L,a<b);
    return 1;
}

static int
luaminiflac_uint64__le(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    lua_pushboolean(L,a<=b);
    return 1;
}

static int
luaminiflac_uint64__band(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a & b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__bor(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a | b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__bxor(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = a ^ b;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__bnot(lua_State *L) {
    uint64_t *o = NULL;
    uint64_t *r = NULL;

    o = lua_touserdata(L,1);

    r = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));

    *r = ~*o;

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__shl(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = (uint64_t)(a << b);

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__shr(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t *res = NULL;

    a = luaminiflac_touint64(L,1);
    b = luaminiflac_touint64(L,2);

    res = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
    *res = (uint64_t)(a >> b);

    luaL_setmetatable(L,luaminiflac_uint64_mt);

    return 1;
}

static int
luaminiflac_uint64__tostring(lua_State *L) {
    char *p;
    char t[21];
    size_t l;

    uint64_t *o = (uint64_t *)lua_touserdata(L,1);
    p = luaminiflac_uint64_to_str(*o,t,&l);
    lua_pushlstring(L,p,l);
    return 1;
}

static int
luaminiflac_uint64__concat(lua_State *L) {
    lua_getglobal(L,"tostring");
    lua_pushvalue(L,1);
    lua_call(L,1,1);

    lua_getglobal(L,"tostring");
    lua_pushvalue(L,2);
    lua_call(L,1,1);

    lua_concat(L,2);
    return 1;
}

static int
luaminiflac_int64__unm(lua_State *L) {
    int64_t *o = NULL;
    int64_t *r = NULL;
    uint64_t *t = NULL;

    o = lua_touserdata(L,1);

    if(*o == ((int64_t) 0x8000000000000000)) {
        t = (uint64_t *)lua_newuserdata(L,sizeof(uint64_t));
        *t = -(*o);
        luaL_setmetatable(L,luaminiflac_uint64_mt);
    } else {
        r = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
        *r = -(*o);
        luaL_setmetatable(L,luaminiflac_int64_mt);
    }

    return 1;
}

static int
luaminiflac_int64__add(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    if(res == NULL) {
        return luaL_error(L,"out of memory");
    }

    *res = a + b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__sub(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a - b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__mul(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a * b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__div(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a / b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__mod(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a % b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__pow(lua_State *L) {
    int64_t base = 0;
    int64_t exp = 0;
    int64_t *res = NULL;
    int64_t result = 1;

    base = luaminiflac_toint64(L,1);
    exp = luaminiflac_toint64(L,2);

    if(exp < 0) {
        return luaL_error(L,"exp must be positive");
    }

    for (;;) {
        if(exp & 1) {
            result *= base;
        }
        exp >>= 1;
        if(!exp) {
            break;
        }
        base *= base;
    }

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = result;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__eq(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    lua_pushboolean(L,a==b);
    return 1;
}

static int
luaminiflac_int64__lt(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    lua_pushboolean(L,a<b);
    return 1;
}

static int
luaminiflac_int64__le(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    lua_pushboolean(L,a<=b);
    return 1;
}

static int
luaminiflac_int64__band(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a & b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__bor(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a | b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__bxor(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = a ^ b;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__bnot(lua_State *L) {
    int64_t *o = NULL;
    int64_t *r = NULL;

    o = lua_touserdata(L,1);

    r = (int64_t *)lua_newuserdata(L,sizeof(int64_t));

    *r = ~*o;

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__shl(lua_State *L) {
    int64_t a = 0;
    int64_t b = 0;
    int64_t *res = NULL;

    a = luaminiflac_toint64(L,1);
    b = luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = (int64_t)(a << b);

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__shr(lua_State *L) {
    uint64_t a = 0;
    uint64_t b = 0;
    int64_t *res = NULL;

    a = (uint64_t)luaminiflac_toint64(L,1);
    b = (uint64_t)luaminiflac_toint64(L,2);

    res = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    *res = (int64_t)(a >> b);

    luaL_setmetatable(L,luaminiflac_int64_mt);

    return 1;
}

static int
luaminiflac_int64__tostring(lua_State *L) {
    char *p;
    char t[22];
    size_t l;

    int64_t *o = (int64_t *)lua_touserdata(L,1);
    p = luaminiflac_int64_to_str(*o,t,&l);
    lua_pushlstring(L,p,l);
    return 1;
}

static int
luaminiflac_int64__concat(lua_State *L) {
    lua_getglobal(L,"tostring");
    lua_pushvalue(L,1);
    lua_call(L,1,1);

    lua_getglobal(L,"tostring");
    lua_pushvalue(L,2);
    lua_call(L,1,1);

    lua_concat(L,2);
    return 1;
}

static int
luaminiflac_int64(lua_State *L) {
    /* create a new int64 object from a number or string */
    int64_t *t = 0;

    t = (int64_t *)lua_newuserdata(L,sizeof(int64_t));
    if(t == NULL) {
        return luaL_error(L,"out of memory");
    }

    if(lua_gettop(L) > 1) {
        *t = luaminiflac_toint64(L,1);
    }
    else {
        *t = 0;
    }

    luaL_setmetatable(L,luaminiflac_int64_mt);
    return 1;
}

static const struct luaL_Reg luaminiflac_int64_metamethods[] = {
    { "__add", luaminiflac_int64__add },
    { "__sub", luaminiflac_int64__sub },
    { "__mul", luaminiflac_int64__mul },
    { "__div", luaminiflac_int64__div },
    { "__idiv", luaminiflac_int64__div },
    { "__mod", luaminiflac_int64__mod },
    { "__pow", luaminiflac_int64__pow },
    { "__unm", luaminiflac_int64__unm },
    { "__band", luaminiflac_int64__band },
    { "__bor", luaminiflac_int64__bor },
    { "__bxor", luaminiflac_int64__bxor },
    { "__bnot", luaminiflac_int64__bnot },
    { "__shl", luaminiflac_int64__shl },
    { "__shr", luaminiflac_int64__shr },
    { "__eq", luaminiflac_int64__eq },
    { "__lt", luaminiflac_int64__lt },
    { "__le", luaminiflac_int64__le },
    { "__tostring", luaminiflac_int64__tostring },
    { "__concat", luaminiflac_int64__concat },
    { NULL, NULL },
};

static const struct luaL_Reg luaminiflac_uint64_metamethods[] = {
    { "__add", luaminiflac_uint64__add },
    { "__sub", luaminiflac_uint64__sub },
    { "__mul", luaminiflac_uint64__mul },
    { "__div", luaminiflac_uint64__div },
    { "__idiv", luaminiflac_uint64__div },
    { "__mod", luaminiflac_uint64__mod },
    { "__pow", luaminiflac_uint64__pow },
    { "__unm", luaminiflac_uint64__unm },
    { "__band", luaminiflac_uint64__band },
    { "__bor", luaminiflac_uint64__bor },
    { "__bxor", luaminiflac_uint64__bxor },
    { "__bnot", luaminiflac_uint64__bnot },
    { "__shl", luaminiflac_uint64__shl },
    { "__shr", luaminiflac_uint64__shr },
    { "__eq", luaminiflac_uint64__eq },
    { "__lt", luaminiflac_uint64__lt },
    { "__le", luaminiflac_uint64__le },
    { "__tostring", luaminiflac_uint64__tostring },
    { "__concat", luaminiflac_uint64__concat },
    { NULL, NULL },
};

/* }}} */
static void
luaminiflac_expand_buffer(lua_State* L, int idx, luaminiflac_t *lFlac, uint32_t len) {
    if(len < lFlac->buffer_len) return;

    lua_getuservalue(L,idx);
    lFlac->buffer = lua_newuserdata(L,len);
    if(lFlac->buffer == NULL) {
        luaL_error(L,"out of memory");
        return;
    }
    lFlac->buffer_len = len;
    lua_setfield(L,-2,"buffer");
    lua_pop(L,1);
}


static void
luaminiflac_push_frame_header(lua_State* L, luaminiflac_t* lFlac) {
    lua_newtable(L);

    lua_pushinteger(L,lFlac->flac.frame.header.blocking_strategy);
    lua_setfield(L,-2,"blocking_strategy");
    lua_pushinteger(L,lFlac->flac.frame.header.block_size);
    lua_setfield(L,-2,"block_size");
    lua_pushinteger(L,lFlac->flac.frame.header.sample_rate);
    lua_setfield(L,-2,"sample_rate");
    lua_pushinteger(L,lFlac->flac.frame.header.channel_assignment);
    lua_setfield(L,-2,"channel_assignment");
    lua_pushinteger(L,lFlac->flac.frame.header.channels);
    lua_setfield(L,-2,"channels");
    lua_pushinteger(L,lFlac->flac.frame.header.bps);
    lua_setfield(L,-2,"bps");
    if(lFlac->flac.frame.header.blocking_strategy) { /* variable blocksize */
        luaminiflac_pushuint64(L,lFlac->flac.frame.header.sample_number);
        lua_setfield(L,-2,"sample_number");
    } else {
        lua_pushinteger(L,lFlac->flac.frame.header.frame_number);
        lua_setfield(L,-2,"frame_number");
    }
    lua_pushinteger(L,lFlac->flac.frame.header.crc8);
    lua_setfield(L,-2,"crc8");
}

static void
luaminiflac_push_header(lua_State* L, luaminiflac_t* lFlac) {
    const char* metadata_type = NULL;
    lua_newtable(L);

    if(lFlac->flac.state == MINIFLAC_METADATA) {
        lua_newtable(L);

        switch(lFlac->flac.metadata.header.type) {
            case MINIFLAC_METADATA_INVALID: {
                metadata_type = luaminiflac_metadata_strs[7];
                break;
            }
            case MINIFLAC_METADATA_UNKNOWN: {
                metadata_type = luaminiflac_metadata_strs[8];
                break;
            }
            default: {
                metadata_type = luaminiflac_metadata_strs[lFlac->flac.metadata.header.type];
                break;
            }
        }
        lua_pushstring(L,metadata_type);
        lua_setfield(L,-2,"type");
        lua_pushboolean(L,lFlac->flac.metadata.header.is_last);
        lua_setfield(L,-2,"is_last");
        lua_pushinteger(L,lFlac->flac.metadata.header.length);
        lua_setfield(L,-2,"length");

        lua_setfield(L,-2,"metadata");

        lua_pushstring(L,"metadata");
        lua_setfield(L,-2,"type");
    } else if(lFlac->flac.state == MINIFLAC_FRAME) {
        lua_newtable(L);

        luaminiflac_push_frame_header(L,lFlac);
        lua_setfield(L,-2,"header");

        lua_setfield(L,-2,"frame");

        lua_pushstring(L,"frame");
        lua_setfield(L,-2,"type");
    } else {
        lua_pushstring(L,"unknown");
        lua_setfield(L,-2,"type");
    }
}

static void
luaminiflac_push_frame_samples(lua_State* L, luaminiflac_t* lFlac) {
    uint8_t channel = 0;
    uint32_t sample = 0;

    lua_createtable(L,lFlac->flac.frame.header.channels,0);
    while(channel<lFlac->flac.frame.header.channels) {
        sample = 0;
        lua_createtable(L,lFlac->flac.frame.header.block_size,0);
        while(sample < lFlac->flac.frame.header.block_size) {
            lua_pushinteger(L,lFlac->samples[channel][sample]);
            lua_rawseti(L,-2,++sample);
        }
        lua_rawseti(L,-2,++channel);
    }
}

static void
luaminiflac_push_frame(lua_State* L, luaminiflac_t* lFlac) {
    lua_newtable(L);

    lua_pushstring(L,"frame");
    lua_setfield(L,-2,"type");

    lua_newtable(L);

    luaminiflac_push_frame_header(L,lFlac);
    lua_setfield(L,-2,"header");

    luaminiflac_push_frame_samples(L,lFlac);
    lua_setfield(L,-2,"samples");

    lua_setfield(L,-2,"frame");
}

static int
luaminiflac_miniflac_t(lua_State *L) {
    lua_Integer container = 0;
    unsigned int c = 0;
    luaminiflac_t *lFlac = NULL;

    container = luaL_optinteger(L,1,(lua_Integer)MINIFLAC_CONTAINER_UNKNOWN);
    switch(container) {
        case MINIFLAC_CONTAINER_UNKNOWN: break;
        case MINIFLAC_CONTAINER_NATIVE: break;
        case MINIFLAC_CONTAINER_OGG: break;
        default:
            return luaL_error(L,"invalid container type");
    }

    lFlac = lua_newuserdata(L,sizeof(luaminiflac_t));
    if(lFlac == NULL) {
        return luaL_error(L,"out of memory");
    }

    for(c=0;c<8;c++) {
        lFlac->samples[c] = &lFlac->samplebuf[c * 65535];
    }

    lFlac->buffer = NULL;
    lFlac->buffer_len = 0;

    miniflac_init(&lFlac->flac,(MINIFLAC_CONTAINER)container);
    luaL_setmetatable(L,luaminiflac_mt);

    lua_newtable(L);
    lua_setuservalue(L,-2);

    luaminiflac_expand_buffer(L,-1,lFlac,1024);

    return 1;
}

static int
luaminiflac_miniflac_init(lua_State *L) {
    luaminiflac_t *lFlac = NULL;
    lua_Integer container = 0;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    container = luaL_optinteger(L,2,(lua_Integer)MINIFLAC_CONTAINER_UNKNOWN);
    switch(container) {
        case MINIFLAC_CONTAINER_UNKNOWN: break;
        case MINIFLAC_CONTAINER_NATIVE: break;
        case MINIFLAC_CONTAINER_OGG: break;
        default:
            return luaL_error(L,"invalid container type");
    }
    miniflac_init(&lFlac->flac,(MINIFLAC_CONTAINER)container);
    return 0;
}

static int
luaminiflac_miniflac_sync(lua_State *L) {
    /*
     * returns result, err, remain
     * on MINIFLAC_CONTINUE, result = false, rem is likely 0 bytes
     * on error, result = nil and err is set
     * err is only set on error, nil otherwise */
    luaminiflac_t *lFlac = NULL;
    const char* str = NULL;
    size_t      len = 0;
    uint32_t   used = 0;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing parameter data");
    }

    r = miniflac_sync(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used);

    switch(r) {
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            lua_pushlstring(L,&str[used],len-used);
            return 3;
        }
        case MINIFLAC_OK: {
            luaminiflac_push_header(L,lFlac);
            lua_pushnil(L);
            lua_pushlstring(L,&str[used],len-used);
            return 3;
        }
        default: break;
    }
    lua_pushnil(L);
    lua_pushinteger(L,r);
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}


static int
luaminiflac_miniflac_decode(lua_State *L) {
    /*
     * returns result, err, rem
     * on MINIFLAC_CONTINUE, result = false, rem is likely 0 bytes
     * on error, result = nil and err is set
     * err is only set on error, nil otherwise
     * result is a multidimensional tables of samples */
    luaminiflac_t *lFlac = NULL;
    const char* str = NULL;
    size_t      len = 0;
    uint32_t   used = 0;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }

    r = miniflac_decode(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,(int32_t**)lFlac->samples);

    switch(r) {
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            lua_pushlstring(L,&str[used],len-used);
            return 3;
        }
        case MINIFLAC_OK: {
            luaminiflac_push_frame(L,lFlac);
            lua_pushnil(L);
            lua_pushlstring(L,&str[used],len-used);
            return 3;
        }
        default: break;
    }
    lua_pushnil(L);
    lua_pushinteger(L,r);
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

/* closure for getting a uint8_t */
static int
luaminiflac_read_uint8(lua_State *L) {
    luaminiflac_t *lFlac      = NULL;
    const char* str           = NULL;
    size_t      len           = 0;
    uint32_t   used           = 0;
    uint8_t     val           = 0;
    luaminiflac_uint8_func f = NULL;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }
    f = (luaminiflac_uint8_func)lua_touserdata(L,lua_upvalueindex(1));

    r = f(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,&val);

    /* treat METADATA_END like CONTINUE, the coroutine interface tracks that we've
     * read the right number of comments / bytes / whatever */
    switch(r) {
        case MINIFLAC_METADATA_END: /* fall-through */
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            break;
        }
        case MINIFLAC_OK: {
            lua_pushinteger(L,val);
            lua_pushnil(L);
            break;
        }
        default: {
            lua_pushnil(L);
            lua_pushinteger(L,r);
            break;
        }
    }
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

/* closure for getting a uint16_t */
static int
luaminiflac_read_uint16(lua_State *L) {
    luaminiflac_t *lFlac      = NULL;
    const char* str           = NULL;
    size_t      len           = 0;
    uint32_t   used           = 0;
    uint16_t    val           = 0;
    luaminiflac_uint16_func f = NULL;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }
    f = (luaminiflac_uint16_func)lua_touserdata(L,lua_upvalueindex(1));

    r = f(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,&val);

    switch(r) {
        case MINIFLAC_METADATA_END: /* fall-through */
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            break;
        }
        case MINIFLAC_OK: {
            lua_pushinteger(L,val);
            lua_pushnil(L);
            break;
        }
        default: {
            lua_pushnil(L);
            lua_pushinteger(L,r);
            break;
        }
    }
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

/* closure for getting a uint32_t */
static int
luaminiflac_read_uint32(lua_State *L) {
    luaminiflac_t *lFlac      = NULL;
    const char* str           = NULL;
    size_t      len           = 0;
    uint32_t   used           = 0;
    uint32_t    val           = 0;
    luaminiflac_uint32_func f = NULL;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }
    f = (luaminiflac_uint32_func)lua_touserdata(L,lua_upvalueindex(1));

    r = f(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,&val);

    switch(r) {
        case MINIFLAC_METADATA_END: /* fall-through */
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            break;
        }
        case MINIFLAC_OK: {
            lua_pushinteger(L,val);
            lua_pushnil(L);
            break;
        }
        default: {
            lua_pushnil(L);
            lua_pushinteger(L,r);
            break;
        }
    }
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

/* closure for getting a uint64_t */
static int
luaminiflac_read_uint64(lua_State *L) {
    luaminiflac_t *lFlac      = NULL;
    const char* str           = NULL;
    size_t      len           = 0;
    uint32_t   used           = 0;
    uint64_t    val           = 0;
    luaminiflac_uint64_func f = NULL;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }
    f = (luaminiflac_uint64_func)lua_touserdata(L,lua_upvalueindex(1));

    r = f(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,&val);

    switch(r) {
        case MINIFLAC_METADATA_END: /* fall-through */
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            break;
        }
        case MINIFLAC_OK: {
            luaminiflac_pushuint64(L,val);
            lua_pushnil(L);
            break;
        }
        default: {
            lua_pushnil(L);
            lua_pushinteger(L,r);
            break;
        }
    }
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

/* closure for getting a string */
static int
luaminiflac_read_str(lua_State *L) {
    luaminiflac_t *lFlac   = NULL;
    const char* str        = NULL;
    size_t      len        = 0;
    uint32_t   used        = 0;
    uint32_t   maxlen      = 0;
    luaminiflac_str_func f = NULL;
    MINIFLAC_RESULT r;

    lFlac = luaL_checkudata(L,1,luaminiflac_mt);
    str   = lua_tolstring(L,2,&len);
    if(str == NULL) {
        return luaL_error(L,"missing data");
    }
    maxlen = luaL_optinteger(L,3,0);
    f = (luaminiflac_str_func)lua_touserdata(L,lua_upvalueindex(1));

    luaminiflac_expand_buffer(L, 1, lFlac, maxlen);

    r = f(&lFlac->flac,(const uint8_t*)str,(uint32_t)len,&used,lFlac->buffer,lFlac->buffer_len,&maxlen);

    switch(r) {
        case MINIFLAC_METADATA_END: /* fall-through */
        case MINIFLAC_CONTINUE: {
            lua_pushboolean(L,0);
            lua_pushnil(L);
            break;
        }
        case MINIFLAC_OK: {
            lua_pushlstring(L,(const char *)lFlac->buffer,maxlen);
            lua_pushnil(L);
            break;
        }
        default: {
            lua_pushnil(L);
            lua_pushinteger(L,r);
            break;
        }
    }
    lua_pushlstring(L,&str[used],len-used);
    return 3;
}

static const luaminiflac_metamethods_t luaminiflac_miniflac_metamethods[] = {
    { "miniflac_init",          "init"   },
    { "miniflac_sync",          "sync"   },
    { "miniflac_decode",        "decode" },

    { "miniflac_streaminfo_min_block_size",    "streaminfo_min_block_size" },
    { "miniflac_streaminfo_max_block_size",    "streaminfo_max_block_size" },
    { "miniflac_streaminfo_min_frame_size",    "streaminfo_min_frame_size" },
    { "miniflac_streaminfo_max_frame_size",    "streaminfo_max_frame_size" },
    { "miniflac_streaminfo_sample_rate",       "streaminfo_sample_rate" },
    { "miniflac_streaminfo_channels",          "streaminfo_channels" },
    { "miniflac_streaminfo_bps",               "streaminfo_bps" },
    { "miniflac_streaminfo_total_samples",     "streaminfo_total_samples" },
    { "miniflac_streaminfo_md5_length",        "streaminfo_md5_length" },
    { "miniflac_streaminfo_md5_data",          "streaminfo_md5_data" },

    { "miniflac_vorbis_comment_vendor_length", "vorbis_comment_vendor_length" },
    { "miniflac_vorbis_comment_vendor_string", "vorbis_comment_vendor_string" },
    { "miniflac_vorbis_comment_total",         "vorbis_comment_total" },
    { "miniflac_vorbis_comment_length",        "vorbis_comment_length" },
    { "miniflac_vorbis_comment_string",        "vorbis_comment_string" },

    { "miniflac_picture_type",                 "picture_type" },
    { "miniflac_picture_mime_length",          "picture_mime_length" },
    { "miniflac_picture_mime_string",          "picture_mime_string" },
    { "miniflac_picture_description_length",   "picture_description_length" },
    { "miniflac_picture_description_string",   "picture_description_string" },
    { "miniflac_picture_width",                "picture_width" },
    { "miniflac_picture_height",               "picture_height" },
    { "miniflac_picture_colordepth",           "picture_colordepth" },
    { "miniflac_picture_totalcolors",          "picture_totalcolors" },
    { "miniflac_picture_length",               "picture_length" },
    { "miniflac_picture_data",                 "picture_data" },

    { "miniflac_cuesheet_catalog_length",      "cuesheet_catalog_length" },
    { "miniflac_cuesheet_catalog_string",      "cuesheet_catalog_string" },
    { "miniflac_cuesheet_leadin",              "cuesheet_leadin" },
    { "miniflac_cuesheet_cd_flag",             "cuesheet_cd_flag" },
    { "miniflac_cuesheet_tracks",              "cuesheet_tracks" },
    { "miniflac_cuesheet_track_offset",        "cuesheet_track_offset" },
    { "miniflac_cuesheet_track_number",        "cuesheet_track_number" },
    { "miniflac_cuesheet_track_isrc_length",   "cuesheet_track_isrc_length" },
    { "miniflac_cuesheet_track_isrc_string",   "cuesheet_track_isrc_string" },
    { "miniflac_cuesheet_track_audio_flag",    "cuesheet_track_audio_flag" },
    { "miniflac_cuesheet_track_preemph_flag",  "cuesheet_track_preemph_flag" },
    { "miniflac_cuesheet_track_indexpoints",   "cuesheet_track_indexpoints" },
    { "miniflac_cuesheet_index_point_offset",  "cuesheet_index_point_offset" },
    { "miniflac_cuesheet_index_point_number",  "cuesheet_index_point_number" },

    { "miniflac_seektable_seekpoints",         "seektable_seekpoints" },
    { "miniflac_seektable_sample_number",      "seektable_sample_number" },
    { "miniflac_seektable_sample_offset",      "seektable_sample_offset" },
    { "miniflac_seektable_samples",            "seektable_samples" },

    { "miniflac_application_id",               "application_id" },
    { "miniflac_application_length",           "application_length" },
    { "miniflac_application_data",             "application_data" },

    { "miniflac_padding_length",               "padding_length" },
    { "miniflac_padding_data",                 "padding_data" },

    { NULL, NULL },
};

#define LMF(a,t) { miniflac_ ## a, luaminiflac_read_ ## t, "miniflac_" #a }

static const luaminiflac_closures_t luaminiflac_closures[] = {
    /*
    void* f;
    lua_CFunction l;
    const char* name;
    */
    LMF(streaminfo_min_block_size,uint16),
    LMF(streaminfo_max_block_size,uint16),
    LMF(streaminfo_min_frame_size,uint32),
    LMF(streaminfo_max_frame_size,uint32),
    LMF(streaminfo_sample_rate,uint32),
    LMF(streaminfo_channels,uint8),
    LMF(streaminfo_bps,uint8),
    LMF(streaminfo_total_samples,uint64),
    LMF(streaminfo_md5_length,uint32),
    LMF(streaminfo_md5_data,str),

    LMF(vorbis_comment_vendor_length,uint32),
    LMF(vorbis_comment_vendor_string,str),
    LMF(vorbis_comment_total,uint32),
    LMF(vorbis_comment_length,uint32),
    LMF(vorbis_comment_string,str),

    LMF(picture_type,uint32),
    LMF(picture_mime_length,uint32),
    LMF(picture_mime_string,str),
    LMF(picture_description_length,uint32),
    LMF(picture_description_string,str),
    LMF(picture_width,uint32),
    LMF(picture_height,uint32),
    LMF(picture_colordepth,uint32),
    LMF(picture_totalcolors,uint32),
    LMF(picture_length,uint32),
    LMF(picture_data,str),

    LMF(cuesheet_catalog_length,uint32),
    LMF(cuesheet_catalog_string,str),
    LMF(cuesheet_leadin,uint64),
    LMF(cuesheet_cd_flag,uint8),
    LMF(cuesheet_tracks,uint8),
    LMF(cuesheet_track_offset,uint64),
    LMF(cuesheet_track_number,uint8),
    LMF(cuesheet_track_isrc_length,uint32),
    LMF(cuesheet_track_isrc_string,str),
    LMF(cuesheet_track_audio_flag,uint8),
    LMF(cuesheet_track_preemph_flag,uint8),
    LMF(cuesheet_track_indexpoints,uint8),
    LMF(cuesheet_index_point_offset,uint64),
    LMF(cuesheet_index_point_number,uint8),

    LMF(seektable_seekpoints,uint32),
    LMF(seektable_sample_number,uint64),
    LMF(seektable_sample_offset,uint64),
    LMF(seektable_samples,uint16),

    LMF(application_id,uint32),
    LMF(application_length,uint32),
    LMF(application_data,str),

    LMF(padding_length,uint32),
    LMF(padding_data,str),

    { NULL, NULL, NULL },
};


static const struct luaL_Reg luaminiflac_functions[] = {
    { "miniflac_int64_t",       luaminiflac_int64                  },
    { "miniflac_uint64_t",      luaminiflac_uint64                 },
    { "miniflac_t",             luaminiflac_miniflac_t             },
    { "miniflac_init",          luaminiflac_miniflac_init          },
    { "miniflac_sync",          luaminiflac_miniflac_sync          },
    { "miniflac_decode",        luaminiflac_miniflac_decode        },
    { NULL,                     NULL                               },
};

LUAMINIFLAC_PUBLIC
int luaopen_miniflac(lua_State *L) {
    const luaminiflac_metamethods_t *miniflac_mm = luaminiflac_miniflac_metamethods;
    const luaminiflac_closures_t *miniflac_closures = luaminiflac_closures;
    unsigned int i = 0;

    lua_newtable(L);

    lua_pushinteger(L,LUAMINIFLAC_VERSION_MAJOR);
    lua_setfield(L,-2,"_VERSION_MAJOR");
    lua_pushinteger(L,LUAMINIFLAC_VERSION_MINOR);
    lua_setfield(L,-2,"_VERSION_MINOR");
    lua_pushinteger(L,LUAMINIFLAC_VERSION_PATCH);
    lua_setfield(L,-2,"_VERSION_PATCH");
    lua_pushliteral(L,LUAMINIFLAC_VERSION);
    lua_setfield(L,-2,"_VERSION");

    lua_newtable(L); /* MINIFLAC_STATE */
    luaminiflac_push_const(OGGHEADER);
    luaminiflac_push_const(STREAMMARKER_OR_FRAME);
    luaminiflac_push_const(STREAMMARKER);
    luaminiflac_push_const(METADATA_OR_FRAME);
    luaminiflac_push_const(METADATA);
    luaminiflac_push_const(FRAME);
    lua_setfield(L,-2,"MINIFLAC_STATE");

    lua_newtable(L); /* MINIFLAC_CONTAINER */
    luaminiflac_push_const(CONTAINER_UNKNOWN);
    luaminiflac_push_const(CONTAINER_NATIVE);
    luaminiflac_push_const(CONTAINER_OGG);
    lua_setfield(L,-2,"MINIFLAC_CONTAINER");

    lua_newtable(L); /* MINIFLAC_RESULT */
    luaminiflac_push_const(SUBFRAME_RESERVED_TYPE);
    luaminiflac_push_const(SUBFRAME_RESERVED_BIT);
    luaminiflac_push_const(STREAMMARKER_INVALID);
    luaminiflac_push_const(RESERVED_CODING_METHOD);
    luaminiflac_push_const(METADATA_TYPE_RESERVED);
    luaminiflac_push_const(METADATA_TYPE_INVALID);
    luaminiflac_push_const(FRAME_RESERVED_SAMPLE_SIZE);
    luaminiflac_push_const(FRAME_RESERVED_CHANNEL_ASSIGNMENT);
    luaminiflac_push_const(FRAME_INVALID_SAMPLE_SIZE);
    luaminiflac_push_const(FRAME_INVALID_SAMPLE_RATE);
    luaminiflac_push_const(FRAME_RESERVED_BLOCKSIZE);
    luaminiflac_push_const(FRAME_RESERVED_BIT2);
    luaminiflac_push_const(FRAME_RESERVED_BIT1);
    luaminiflac_push_const(FRAME_SYNCCODE_INVALID);
    luaminiflac_push_const(FRAME_CRC16_INVALID);
    luaminiflac_push_const(FRAME_CRC8_INVALID);
    luaminiflac_push_const(ERROR);
    luaminiflac_push_const(CONTINUE);
    luaminiflac_push_const(OK);
    luaminiflac_push_const(METADATA_END);
    lua_setfield(L,-2,"MINIFLAC_RESULT");

    lua_newtable(L); /* MINIFLAC_METADATA_TYPE */
    luaminiflac_push_const(METADATA_UNKNOWN);
    luaminiflac_push_const(METADATA_STREAMINFO);
    luaminiflac_push_const(METADATA_PADDING);
    luaminiflac_push_const(METADATA_APPLICATION);
    luaminiflac_push_const(METADATA_SEEKTABLE);
    luaminiflac_push_const(METADATA_VORBIS_COMMENT);
    luaminiflac_push_const(METADATA_CUESHEET);
    luaminiflac_push_const(METADATA_PICTURE);
    luaminiflac_push_const(METADATA_INVALID);
    lua_setfield(L,-2,"MINIFLAC_METADATA_TYPE");

    luaminiflac_push_const(OGGHEADER);
    luaminiflac_push_const(STREAMMARKER_OR_FRAME);
    luaminiflac_push_const(STREAMMARKER);
    luaminiflac_push_const(METADATA_OR_FRAME);
    luaminiflac_push_const(METADATA);
    luaminiflac_push_const(FRAME);

    luaminiflac_push_const(CONTAINER_UNKNOWN);
    luaminiflac_push_const(CONTAINER_NATIVE);
    luaminiflac_push_const(CONTAINER_OGG);

    luaminiflac_push_const(SUBFRAME_RESERVED_TYPE);
    luaminiflac_push_const(SUBFRAME_RESERVED_BIT);
    luaminiflac_push_const(STREAMMARKER_INVALID);
    luaminiflac_push_const(RESERVED_CODING_METHOD);
    luaminiflac_push_const(METADATA_TYPE_RESERVED);
    luaminiflac_push_const(METADATA_TYPE_INVALID);
    luaminiflac_push_const(FRAME_RESERVED_SAMPLE_SIZE);
    luaminiflac_push_const(FRAME_RESERVED_CHANNEL_ASSIGNMENT);
    luaminiflac_push_const(FRAME_INVALID_SAMPLE_SIZE);
    luaminiflac_push_const(FRAME_INVALID_SAMPLE_RATE);
    luaminiflac_push_const(FRAME_RESERVED_BLOCKSIZE);
    luaminiflac_push_const(FRAME_RESERVED_BIT2);
    luaminiflac_push_const(FRAME_RESERVED_BIT1);
    luaminiflac_push_const(FRAME_SYNCCODE_INVALID);
    luaminiflac_push_const(FRAME_CRC16_INVALID);
    luaminiflac_push_const(FRAME_CRC8_INVALID);
    luaminiflac_push_const(ERROR);
    luaminiflac_push_const(CONTINUE);
    luaminiflac_push_const(OK);
    luaminiflac_push_const(METADATA_END);

    luaminiflac_push_const(METADATA_UNKNOWN);
    luaminiflac_push_const(METADATA_STREAMINFO);
    luaminiflac_push_const(METADATA_PADDING);
    luaminiflac_push_const(METADATA_APPLICATION);
    luaminiflac_push_const(METADATA_SEEKTABLE);
    luaminiflac_push_const(METADATA_VORBIS_COMMENT);
    luaminiflac_push_const(METADATA_CUESHEET);
    luaminiflac_push_const(METADATA_PICTURE);
    luaminiflac_push_const(METADATA_INVALID);

    luaL_setfuncs(L,luaminiflac_functions,0);

    while(miniflac_closures->f != NULL) {
        lua_pushlightuserdata(L,miniflac_closures->f);
        lua_pushcclosure(L,miniflac_closures->l,1);
        lua_setfield(L,-2,miniflac_closures->name);
        miniflac_closures++;
    }

    luaL_newmetatable(L,luaminiflac_mt);
    lua_newtable(L); /* __index */
    while(miniflac_mm->name != NULL) {
        lua_getfield(L,-3,miniflac_mm->name);
        lua_setfield(L,-2,miniflac_mm->metaname);
        miniflac_mm++;
    }
    lua_setfield(L,-2,"__index");
    lua_pop(L,1);

    lua_newtable(L); /* our _metamethods table */
    miniflac_mm = luaminiflac_miniflac_metamethods;
    while(miniflac_mm->metaname != NULL) {
        lua_pushstring(L,miniflac_mm->metaname);
        lua_rawseti(L,-2,++i);
        miniflac_mm++;
    }
    lua_setfield(L,-2,"_metamethods");

    lua_getfield(L,-1,"miniflac_int64_t");
    lua_setfield(L,-2,"int64_t");

    lua_getfield(L,-1,"miniflac_uint64_t");
    lua_setfield(L,-2,"uint64_t");

    luaL_newmetatable(L,luaminiflac_int64_mt);
    luaL_setfuncs(L,luaminiflac_int64_metamethods,0);
    lua_pop(L,1);

    luaL_newmetatable(L,luaminiflac_uint64_mt);
    luaL_setfuncs(L,luaminiflac_uint64_metamethods,0);
    lua_pop(L,1);

    return 1;
}
