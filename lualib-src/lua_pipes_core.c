#include "lua_pipes_core.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>

#include "pipes_malloc.h"
#include "pipes_api.h"

#include "lua_pipes_env.h"

#include "lua_pipes_api.h"
#include "lua_pipes_socket.h"
#include "lua_pipes_json.h"
#include "pipes.h"
#include "lua_seri.h"
#include "lua_pipes_malloc.h"


void luapps_add_thread_mem(struct lua_service_context* ctx, int dlt)
{
	struct pipes_thread_context* th = ctx->pipes_ctx->thread;
	th->mem += dlt;
	//printf("add_thread_mem, idx=%d, dlt=%d, cur=%ld\n", th->idx, dlt, th->mem);
}
static int
optboolean(const char *key, int opt) {
	const char * str = lua_pipes_getenv(key);
	if (str == NULL) {
		lua_pipes_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str, "true") == 0;
}

static const char *
optstring(const char *key, const char * opt) {
	const char * str = lua_pipes_getenv(key);
	if (str == NULL) {
		if (opt) {
			lua_pipes_setenv(key, opt);
			opt = lua_pipes_getenv(key);
		}
		return opt;
	}
	return str;
}

static void* fn_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	struct lua_service_context* ctx = (struct lua_service_context*)ud;
	//printf("lua mem: %lu\n", ctx->mem);
	if (nsize == 0)
	{
		if (ptr)
		{
			pipes_free(ptr);
			ctx->mem -= osize;
		}
		return NULL;
	}
	else
	{	
		void* ptrNew = pipes_realloc(ptr, nsize);
		if (ptrNew)
		{
			if (ptr)
			{
				ctx->mem += nsize - osize;
			}
			else
			{	
				ctx->mem += nsize;
			}
		}
		return ptrNew;
	}	
}

static void destroy_service_param(struct lua_service_context* ctx)
{
	if (ctx)
	{
		if (ctx->param)
		{
			luapps_free(ctx->param, 1);
			ctx->param = NULL;
			ctx->param_size = 0;
		}
	}
}

static int send_log(struct pipes_service_context* caller, const char* msg, size_t len)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	//
	uint64_t to = pipes_handle_findname(LOGGER_NAME, global->handle_mgr);
	if (to != 0 && msg)
	{ 
		void* ptr = luapps_malloc(len);
		memcpy(ptr, msg, len);
		pipes_api_send(caller, to, PMSG_TYPE_DATA_STR, 0, ptr, len);
		return 1;
	}
	return 0;
}

static int init_lua_state(struct lua_service_context* ctx)
{
	int ret = -1;
	do
	{
		lua_State* L = lua_newstate(fn_lua_alloc, ctx);
		//lua_State* L = luaL_newstate();
		//
		ctx->L = L;
		luaL_openlibs(L);
		//
		lua_pushlightuserdata(L, ctx);
		lua_setfield(L, LUA_REGISTRYINDEX, PIPES_CONTEXT);
		
		/*// set context info
		struct pipes_thread_context* thCtx = (struct pipes_thread_context*)(ctx->pipes_ctx->thread);
		struct pipes_global_context* global = thCtx->global;
		lua_createtable(L, 0, 2);
		lua_pushinteger(L, ctx->pipes_ctx->handle);
		lua_setfield(L, -2, "id");
		lua_pushinteger(L, global->worker_thread_num);
		lua_setfield(L, -2, "totalThread");
		lua_pushinteger(L, thCtx->idx);
		lua_setfield(L, -2, "thread");
		lua_setglobal(L, "LPPS_CONTEXT_INFO");
		*/
		//
		lua_pushstring(L, LOGGER_NAME);
		lua_setglobal(L, "LOGGER_NAME");
		//lua_pushstring(L, DEFAULT_MSG_PROCESSOR);
		//lua_setglobal(L, "DEF_MSG_HANDLER");
		//
		const char *path = optstring("lua_path", "./lualib/?.lua");
		lua_pushstring(L, path);
		lua_setglobal(L, "LUA_PATH");
		const char *cpath = optstring("lua_cpath", "./lualib-src/?.so");
		lua_pushstring(L, cpath);
		lua_setglobal(L, "LUA_CPATH");
		const char *service = optstring("lua_service", "./service/?.lua");
		lua_pushstring(L, service);
		lua_setglobal(L, "LUA_SERVICE");
		// register openlib api
		lua_pushcfunction(L, luapps_api_openlib);
		lua_setglobal(L, LUA_OPENLIB_FUNC);
		lua_pushcfunction(L, luapps_socket_openlib);
		lua_setglobal(L, LUA_OPENSOCK_FUNC);
		lua_pushcfunction(L, luapps_json_openlib);
		lua_setglobal(L, LUA_OPENJSON_FUNC);
		//
		const char * loader = optstring("lua_loader", "./lualib/lpps_service_loader.lua");
		int r = luaL_loadfile(L, loader);
		if (r != LUA_OK) {
			size_t len = 0;
			const char* err = lua_tolstring(L, -1, &len);
			if (send_log(ctx->pipes_ctx, err, len))  // send to logger succ
			{
				
			}
			else
			{
				printf("can't load lua: %s, %s", loader, err);
			}
			ret = 1;
			break;
		}
		// push param
		int argNum = 1;
		lua_pushstring(L, ctx->src_path);
		
		if (ctx->param)
		{
			argNum += 3;
			int type = pipes_api_dec_msgtype(ctx->param_size);
			uint32_t szRaw = pipes_api_msg_sizeraw(ctx->param_size);
			lua_pushinteger(L, type);
			lua_pushlightuserdata(L, ctx->param);
			lua_pushinteger(L, szRaw);
		}
		// call
		r = lua_pcall(L, argNum, 0, 0);
		destroy_service_param(ctx);
		if (r != LUA_OK) {
			size_t len = 0;
			const char* err = lua_tolstring(L, -1, &len);
			if (send_log(ctx->pipes_ctx, err, len))  // send to logger succ
			{
				
			}
			else
			{
				printf("lua pcall error : %s\n", err);
			}
			ret = 2;
			break;
		}
		ret = 0;
	} while (0);
	return ret;
}

static void destroy_lua_service(struct lua_service_context* ctx)
{
	lua_State* L = ctx->L;
	if (L)
	{
		/*
		int t = lua_getglobal(L, "LPPS_PARAM_COLLECTED");
		if (t != LUA_TNIL)  // paramData has collected by lua-state
		{
			ctx->param = NULL;
			ctx->param_size = 0;                                                                                                                           
		}
		*/
		int64_t memPre = ctx->mem;
		lua_close(L);
		ctx->L = NULL;
		luapps_add_thread_mem(ctx, ctx->mem - memPre);  // update thread mem
	}
	if (ctx->src_path)
	{
		pipes_free(ctx->src_path);
		ctx->src_path = NULL;
	}
	destroy_service_param(ctx);
	pipes_free(ctx);
}
static struct lua_service_context* create_service(const char* srcPath, void* param, size_t paramSize, int type)
{
	size_t sz = sizeof(struct lua_service_context);
	struct lua_service_context* ctx = pipes_malloc(sz);
	memset(ctx, 0, sz);
	//
	size_t szPath = strlen(srcPath) + 1;
	ctx->src_path = pipes_malloc(szPath);
	strcpy(ctx->src_path, srcPath);
	// copy param
	if(param)
	{	
		ctx->param = luapps_malloc(paramSize); 
		memcpy(ctx->param, param, paramSize);
		ctx->param_size = pipes_api_enc_msgtype(type, paramSize);
	}
	return ctx;
};

static int on_msg_cb(
	struct lua_service_context* lctx,
	lua_Integer cb,
	int type,
	int session,
	uint64_t source,
	void* data,
	uint32_t size)
{
	int64_t memPre = lctx->mem;
	//
	lua_State* L = lctx->L;
	int msgConsumed = 0;
	int fnType = lua_rawgeti(L, LUA_REGISTRYINDEX, cb);
	if (fnType == LUA_TFUNCTION) // has reg msg cb
		{
			// source, session, type, data(opt), size(opt)
			int argNum = 3;
			lua_pushinteger(L, source);
			lua_pushinteger(L, session);
			lua_pushinteger(L, type);
			if (data)
			{
				lua_pushlightuserdata(L, data);
			}
			else
			{
				lua_pushnil(L);
			}
			lua_pushinteger(L, size);
			argNum += 2;
			//
			int callRet = lua_pcall(L, argNum, 1, 0);
			if (callRet != LUA_OK)
			{
				size_t len = 0;
				const char* err = lua_tolstring(L, -1, &len);
				if (send_log(lctx->pipes_ctx, err, len))  // send to logger succ
				{
					
				}
				else
				{
					printf("lua_on_msg err: %s\n", err);
				}
				if (callRet > 0)
				{
					msgConsumed = -callRet;
				}
				else
				{	
					msgConsumed = callRet;
				}
			}
			else // no error
			{
				msgConsumed = lua_toboolean(L, -1);
			}
		}
	luapps_add_thread_mem(lctx, lctx->mem - memPre);  // update thread mem
	return msgConsumed;
}

//
uint64_t luapps_create_service(
	struct lua_service_context* caller, 
	int toThread, int session,
	const char*name, const char* srcPath, 
	int paramType, void* param, size_t paramSize)
{
	struct lua_service_context* ctx = create_service(srcPath, param, paramSize, paramType);
	uint64_t handle = 0;
	int ret = pipes_api_create_service(caller->pipes_ctx, toThread, session, ctx, name, &handle);
	if (!ret)  // carete pipes-context failed
	{
		destroy_lua_service(ctx);
		handle = 0;
	}
	return handle;
}

void* luapps_on_boot_create(int curThread)
{
	//printf("on bootservice create, curThread=%d\n", curThread);
	const char* bootService = lua_pipes_getenv("boot");
	const char* service = "lpps_launch";
	return create_service(service, (void*)bootService, strlen(bootService), PMSG_TYPE_DATA_STR);
}
int luapps_on_service_init(void* adapter, struct pipes_service_context* service)
{
	struct lua_service_context* ctx = (struct lua_service_context*)adapter;
	ctx->pipes_ctx = service;
	//
	int64_t memPre = ctx->mem;
	int ret = init_lua_state(ctx);
	luapps_add_thread_mem(ctx, ctx->mem - memPre);   //update thread mem
	//
	if(ret != 0)  // init lua_state error
	{
		return 0;
	}
	return 1;
}
void luapps_on_service_exit(void* adapter)
{
	struct lua_service_context* ctx = (struct lua_service_context*)adapter;
	if (ctx->L)
	{
		//call exit
		//on_msg_cb(ctx, ctx->fn_msg_cb, PMSG_TYPE_EXIT_SERVICE, 0, 0, NULL, 0);
		//call finalize
		//on_msg_cb(ctx, ctx->fn_msg_cb, PMSG_TYPE_FINAL_SERVICE, 0, 0, NULL, 0);
	}
	destroy_lua_service(ctx);
}
void luapps_destroy_adapter(void* adapter)
{
	struct lua_service_context* ctx = (struct lua_service_context*)adapter;
	destroy_lua_service(ctx);
}

int luapps_on_message(
	struct pipes_service_context* ctx,
	int type,
	int session,
	uint64_t source,
	void* data,
	uint32_t size)
{
	struct lua_service_context* lctx = pipes_api_get_adapter(ctx);
	if (lctx == NULL)
	{
		return 0;
	}
	lua_State* L = lctx->L;
	if (L == NULL)
	{
		return 0;
	}
	source = pipes_handle_id2local(source);  // localid
	int consumed = on_msg_cb(lctx, lctx->fn_msg_cb, type, session, source, data, size);
	if (consumed < 0)  // lua error, exit service
	{
		//pipes_api_exit_service(ctx);
		consumed = 1;
	}
	return consumed;
}

void* luapps_copy_string(const char* ori, size_t* sz)
{
	size_t oriLen = strlen(ori);
	void * out = luapps_malloc(oriLen);
	memcpy(out, ori, oriLen);
	*sz = oriLen;
	return out;
}

void luapps_on_sys_init()
{
}