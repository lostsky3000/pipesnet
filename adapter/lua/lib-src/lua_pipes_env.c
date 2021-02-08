#include "lua_pipes_env.h"

#include "spinlock.h"
#include "pipes_malloc.h"
#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct lua_pipes_env
{
	struct spinlock lock;
	lua_State* L;
};

static struct lua_pipes_env* _E = NULL;

const char* lua_pipes_getenv(const char* key)
{
	SPIN_LOCK(_E);
	lua_State* L = _E->L;
	lua_getglobal(L, key);
	const char* val = lua_tostring(L, -1);
	lua_pop(L, 1);
	SPIN_UNLOCK(_E);
	return val;
}

void lua_pipes_setenv(const char* key, const char* value)
{
	SPIN_LOCK(_E);
	lua_State* L = _E->L;
	lua_getglobal(L, key);
	assert(lua_isnil(L, -1));
	lua_pop(L, 1);
	lua_pushstring(L, value);
	lua_setglobal(L, key);
	
	SPIN_UNLOCK(_E);
}

int lua_pipes_envexist(const char* key)
{
	SPIN_LOCK(_E);
	lua_State* L = _E->L;
	lua_getglobal(L, key);
	int notExist = lua_isnil(L, -1);
	lua_pop(L, 1);
	SPIN_UNLOCK(_E);
	return !notExist;
}

void lua_pipes_env_init()
{
	assert(_E == NULL);
	_E = pipes_malloc(sizeof(*_E));
	SPIN_INIT(_E);
	_E->L = luaL_newstate();
}

void lua_pipes_env_destroy()
{
	//assert(_E);
	if(_E)
	{
		lua_close(_E->L);
		SPIN_DESTROY(_E);
		pipes_free(_E);
		_E = NULL;
	}
}


