#ifndef LUA_PIPES_ENV_H
#define LUA_PIPES_ENV_H


const char* lua_pipes_getenv(const char* key);

void lua_pipes_setenv(const char* key, const char* value);

int lua_pipes_envexist(const char* key);

void lua_pipes_env_init();

void lua_pipes_env_destroy();


#endif // !LUA_PIPES_ENV_H

