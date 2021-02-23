
#include "lua_pipes_start.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "pipes_start.h"
#include "pipes_plat.h"
#include "pipes_api.h"
#include "lua_pipes_env.h"
#include "lua_pipes_core.h"


#define CFG_LOADER_PATH "lualib/lpps_config_loader.lua"
#define CFG_LOAD_RET_NAME "cfg_load_result"

#define DEF_MIN_WORKER 4


static int
optint(const char *key, int opt) {
	const char * str = lua_pipes_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp, "%d", opt);
		lua_pipes_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

static int init_env(lua_State* L)
{
	int ret = -1;
	do
	{
		if (lua_gettop(L) != 1 || !lua_istable(L, -1))
		{
			fprintf(stderr, "load_config() need return table\n");
			break;
		}
		lua_pipes_env_init();
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{	
			int keyt = lua_type(L, -2);
			if (keyt != LUA_TSTRING) //key must be string
				{
					fprintf(stderr, "config table invalid, keytype=%d\n", keyt);
					lua_pop(L, 2);
					return ret;
				}
			const char* key = lua_tostring(L, -2);
			if (lua_pipes_envexist(key))
			{
				fprintf(stderr, "duplicate key: %s\n", key);
				lua_pop(L, 2);
				return ret;
			}
			if (lua_type(L, -1) == LUA_TBOOLEAN) {
				int b = lua_toboolean(L, -1);
				lua_pipes_setenv(key, b ? "true" : "false");
			}
			else {
				const char * value = lua_tostring(L, -1);
				if (value == NULL) {
					fprintf(stderr, "config table invalid, key = %s\n", key);
					lua_pop(L, 2);
					return ret;
				}
				lua_pipes_setenv(key, value);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		ret = 0;
	} while (0);
	return ret;
}

//
static int _start()
{
	int ret = -1;
	lua_State* L = NULL;
	do
	{	
		// load config
		L = luaL_newstate();
		luaL_openlibs(L);
		if (luaL_loadfile(L, CFG_LOADER_PATH) != LUA_OK)
		{
			fprintf(stderr, "load config loader failed(1): %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
			break;
		}
		if (lua_pcall(L, 0, 0, 0) != LUA_OK)
		{
			fprintf(stderr, "load config loader failed(2): %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
			break;
		}
		if (lua_getglobal(L, CFG_LOAD_RET_NAME) != LUA_TTABLE) // load func not found
			{
				fprintf(stderr, "no result found which named '%s'\n", CFG_LOAD_RET_NAME);
				break;
			}
		// init env
		if((ret = init_env(L)) != 0)
		{
			fprintf(stderr, "init env failed: %d\n", ret);
			break;
		}
		ret = -1;
		// gen start config
		struct pipes_start_config config;
		memset(&config, 0, sizeof(config));
		int cpuNum = pipes_plat_cpunum();
		int defWorkerNum = cpuNum - pipes_api_corethreadnum();
		defWorkerNum = defWorkerNum < DEF_MIN_WORKER ? DEF_MIN_WORKER : defWorkerNum;
		
		config.worker_num = optint("thread", defWorkerNum);
		if (config.worker_num < 1)
		{
			fprintf(stderr, "invalid config, thread=%d\n", config.worker_num);
			break;
		}
		int harbor = optint("harbor", 0);
		/*
		if (harbor < 1 || harbor > 255)
		{
			fprintf(stderr, "invalid config, harbor=%d\n", harbor);
			break;
		}*/
		harbor = 1;
		// set work-dir
		const char * workDir = lua_pipes_getenv("work_dir");
		if (workDir)
		{
			//./service/?.lua
			char buf[256];
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "./%s/?.lua", workDir);
			lua_pipes_setenv("lua_service", buf);
		}
		//
		config.harbor = harbor;
		//
		config.adapter_cfg.boot_create_cb = luapps_on_boot_create;
		config.adapter_cfg.service_init_cb = luapps_on_service_init;
		config.adapter_cfg.service_exit_cb = luapps_on_service_exit;
		config.adapter_cfg.msg_cb = luapps_on_message;
		config.adapter_cfg.destroy_adapter = luapps_destroy_adapter;
		
		fprintf(stdout,
			"harbor=%d, workerThreadNum=%d, cpuNum=%d, defWorkerThreadNum=%d\n", 
			config.harbor,
			config.worker_num,
			cpuNum,
			defWorkerNum);
		//
		luapps_on_sys_init();
		//
		ret = pipes_start(&config);
		if (ret != 0)
		{
			break;
		}
		
		ret = 0;
	} while (0);
	if (L)
	{
		lua_close(L);
		L = NULL;
	}
	lua_pipes_env_destroy();
	
	return ret;
}

//
int luapps_start_main(int argc, char *argv[])
{
	int ret = -1;
	do
	{
		int err = _start();
		if (err)
		{
			fprintf(stderr, "pipes start failed: %d\n", err);
			ret = 1;
			break;
		}
		
		ret = 0;
	} while (0);
	
	return ret;
}


