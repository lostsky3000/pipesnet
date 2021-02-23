#include "lua_pipes_api.h"

#include "lualib.h"
#include "lauxlib.h"

#include "lua_pipes_core.h"
#include "lua_seri.h"
#include "pipes_api.h"
#include "pipes.h"
#include "lua_pipes_malloc.h"
#include "pipes_mq.h"
#include "lua_pipes_env.h"
#include "pipes_runtime.h"

#include <string.h>

#define FN_PACK_PTR_ITER "LPPS_PACK_PTR_ITER"

static int reg_cb(lua_State* L, int* cb)
{
	luaL_checktype(L, -1, LUA_TFUNCTION);
	int type = lua_rawgeti(L, LUA_REGISTRYINDEX, *cb);
	lua_pop(L, 1); 
	int setSucc = 0;
	if (type != LUA_TFUNCTION)  //  cb has not set
		{
			*cb = luaL_ref(L, LUA_REGISTRYINDEX);
			setSucc = 1;
		}
	return setSucc;
}

static int 
l_dispatch(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	//
	int setSucc = reg_cb(L, &ctx->fn_msg_cb);
	lua_pop(L, lua_gettop(L));
	//
	lua_pushboolean(L, setSucc);
	return 1;
}

// pack
struct pack_context
{
	struct lua_service_context* lctx;
	int ptrNum;
}
;
static void* pack_realloc(
	void* ptr,
	size_t szOldData,
	size_t szNewPrefer,
	size_t szNewMin,
	size_t* szNewActual,
	void* udata)
{
	struct lua_service_context* ctx = ((struct pack_context*)udata)->lctx;
	struct pipes_thread_context* thread = pipes_api_getthread(ctx->pipes_ctx);
	void* bufOut = pipes_api_threadlocal_realloc(thread, ptr, szOldData, szNewPrefer, szNewMin, szNewActual);
	return bufOut;
}
static void pack_free(void* ptr, void* udata)
{
	struct lua_service_context* ctx = ((struct pack_context*)udata)->lctx;
	struct pipes_thread_context* thread = pipes_api_getthread(ctx->pipes_ctx);
	pipes_api_threadlocal_free(thread, ptr);
}                    

static void on_luapack_ptr_cb(void* ptr, void* udata)
{
	if (ptr)
	{
		struct pack_context* ctx = (struct pack_context*)udata;
		lua_State* L = ctx->lctx->L;
		++ctx->ptrNum;
		int top = lua_gettop(L);
		int t = lua_getglobal(L, FN_PACK_PTR_ITER);
		if (t != LUA_TFUNCTION)
		{
			luaL_error(L, "no func LPPS_FN_PACK_PTR_ITER found");
			return;
		}
		lua_pushlightuserdata(L, ptr);
		lua_pushinteger(L, ctx->ptrNum);
		if (lua_pcall(L, 2, 0, 0) != LUA_OK)
		{
			printf("pcall pack_ptr_iter error: %s\n", lua_tostring(L, -1));
		}
	}
}
static int 
l_luapack(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	//
	struct pipes_thread_context* thread = pipes_api_getthread(ctx->pipes_ctx);
	size_t szBuf = 0;
	void* buf = pipes_api_threadlocal_malloc(thread, 2048, &szBuf);
	//
	struct pack_context pctx;
	pctx.lctx = ctx;
	pctx.ptrNum = 0;
	//
	size_t szRet = 0;
	void* bufRet = lua_seri_pack(L, 1, &szRet, buf, szBuf, &pctx, pack_realloc, pack_free, NULL); // on_luapack_ptr_cb);
	// copy memory
	if(szRet == 0 || bufRet == NULL)
	{
		//return luaL_error(L, "seri failed");
		pipes_api_threadlocal_free(thread, buf);
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 2;
	}
	//
	void* bufOut = luapps_malloc(szRet);
	memcpy(bufOut, bufRet, szRet);
	pipes_api_threadlocal_free(thread, bufRet);
	//
	lua_pushlightuserdata(L, bufOut);
	lua_pushinteger(L, szRet);
	return 2;
}
static int 
l_malloc(lua_State* L)
{
	lua_Integer sz = luaL_checkinteger(L, 1);
	if (sz < 1)
	{
		return luaL_error(L, "invliad malloc size: %d", sz);
	}
	void* ptr = NULL;
	int isNum = 0;
	lua_Integer ref = lua_tointegerx(L, 2, &isNum);
	if (isNum)
	{
		if (ref < 1)
		{
			return luaL_error(L, "invalid malloc ref: %d", ref);
		}
		ptr = luapps_malloc(sz); 
		int off = ref - 1;
		if (off > 0)
		{
			luapps_retain(ptr, off);
		}
	}
	else
	{
		ptr = luapps_malloc(sz); 
	}
	if (ptr)
	{
		lua_pushlightuserdata(L, ptr);
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}
static int 
l_free(lua_State* L)
{
	if (!lua_islightuserdata(L, 1))
	{
		return luaL_error(L, "free: arg#1 is not lightuserdata");
	}
	int ret = 0;
	void* ptr = lua_touserdata(L, 1);
	if (ptr)
	{
		int isNum = 0;
		lua_Integer ref = lua_tointegerx(L, 2, &isNum);
		if (isNum)
		{
			if (ref < 1)
			{
				return luaL_error(L, "invalid free ref: %d", ref);
			}	
			ret = luapps_free(ptr, ref);
		}
		else
		{
			ret = luapps_free(ptr, 1);
		}
	}
	lua_pushinteger(L, ret);
	return 1;
}

static int 
l_retain(lua_State* L)
{
	if (!lua_islightuserdata(L, 1))
	{
		return luaL_error(L, "retain: arg#1 is not lightuserdata");
	}
	int ret = 0;
	void* ptr = lua_touserdata(L, 1);
	if (ptr)
	{
		int isNum = 0;
		lua_Integer ref = lua_tointegerx(L, 2, &isNum);
		if (isNum)
		{
			if (ref < 1)
			{
				return luaL_error(L, "invalid retain ref: %d", ref);
			}	
			ret = luapps_retain(ptr, ref);
		}
		else
		{
			ret = luapps_retain(ptr, 1);
		}
	}
	lua_pushinteger(L, ret);
	return 1;
}
static int l_exit(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	//
	int ret = pipes_api_exit_service(ctx->pipes_ctx);
	lua_pushboolean(L, ret);
	return 1;
}
static int l_send_bak(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	// to(handle or name), session, type, void*, size, priority
	int t = lua_type(L, 1);
	lua_Integer to;
	if (t == LUA_TNUMBER)  // handle
		{
			to = luaL_checkinteger(L, 1);
		}
	else if (t == LUA_TSTRING) // name
		{
			const char* name = luaL_checkstring(L, 1);
			if (name == NULL)
			{
				return luaL_error(L, "invalid dest name");
			}
			to = pipes_api_gethandle_byname(ctx->pipes_ctx, name);
			if (to == 0)  // service not exist with this name, send to cur-thread-service
			{
				struct pipes_worker_thread_context* wkCtx = (struct pipes_worker_thread_context*)ctx->pipes_ctx->thread;
				to = wkCtx->sys_service->handle;
			}
		}
	else  // invalid type
	{
		return luaL_error(L, "invalid dest: handle or name required");
	}
	lua_Integer session = luaL_checkinteger(L, 2);
	lua_Integer type = luaL_checkinteger(L, 3);
	void* data = NULL;
	size_t sz = 0;
	if (type == PMSG_TYPE_DATA_STR)  // 
		{
			const char * str = lua_tolstring(L, 4, &sz);
			if (str)
			{
				data = luapps_malloc(sz);
				memcpy(data, str, sz);
			}
		}
	else
	{
		data = lua_touserdata(L, 4);
		if (data)  // read bin len
			{
				sz = luaL_checkinteger(L, 5);
			}
	}
	int ret;
	if (lua_toboolean(L, 6))   // priority
		{
			ret = pipes_api_send_priority(ctx->pipes_ctx, to, type, session, data, sz);
		}
	else
	{
		ret = pipes_api_send(ctx->pipes_ctx, to, type, session, data, sz);
	}
	if (ret) // send succ
		{
			lua_pushboolean(L, 1);
		}
	else  // send failed
	{
		lua_pushboolean(L, 0);
	}
	return 1;
}

static int newservice(lua_State* L, struct lua_service_context* ctx, int from)
{
	// srcPath, toThread, session,  paramType(opt), param(opt), paramSize(opt) 
	const char* srcPath = lua_tostring(L, from + 0);
	if (srcPath == NULL)
	{
		return luaL_error(L, "no src-path specified in newservice");
	}
	int toThread = luaL_checkinteger(L, from + 1);
	struct pipes_global_context* g = ctx->pipes_ctx->thread->global;
	if (toThread < 0 || toThread >= g->worker_thread_num)  // toThread invalid, use default
	{
		toThread = pipes_rt_alloc_thread(g, 0);
	}
	int session = luaL_checkinteger(L, from + 2);
	int isNum = 0;
	int type = lua_tointegerx(L, from + 3, &isNum);
	if (!isNum)  
	{
		return luaL_error(L, "invalid msg-type in newservice: %s", lua_typename(L, from + 3));
	}
	void* param = lua_touserdata(L, from + 4);
	size_t szParam = 0;
	if (param) // has param, get paramSize
		{
			szParam = luaL_checkinteger(L, from + 5);
		}
	uint64_t handle = luapps_create_service(ctx, toThread, session, NULL, srcPath, type, param, szParam);
	if (handle > 0)  // succ
		{
			handle = pipes_handle_id2local(handle);  // localid
			lua_pushinteger(L, handle);
		}
	else  // failed
		{
			lua_pushnil(L);
		}
	return 1;
}

static int timeout(lua_State* L, struct lua_service_context* ctx, int from)
{
	int delay = luaL_checkinteger(L, from + 0);
	if (delay < 0)
	{
		return luaL_error(L, "invalid delay: ", delay);
	}
	//
	int isNum = 0;
	int session = lua_tointegerx(L, from + 1, &isNum);
	if (!isNum)  // not specify session
		{
			session = 0;
		}
	//
	int lastCnt = lua_tointegerx(L, from + 2, &isNum);
	if (isNum)  //  specify lastCnt
		{
			if (lastCnt < 0)
			{
				return luaL_error(L, "invalid timeout lastCnt: %d", lastCnt);
			}
		}
	else
	{
		lastCnt = 0;
	}
	//
	int ret = pipes_api_timeout(ctx->pipes_ctx, delay, session, lastCnt);
	lua_pushboolean(L, ret);
	return 1;
}
static int send(lua_State* L, struct lua_service_context* ctx, int from)
{	
	// to, session, mType, msg, sz
	int tp = lua_type(L, from + 0);
	uint64_t to = 0;
	if (tp == LUA_TSTRING)  // use name
		{
			const char* name = lua_tostring(L, from + 0);
			struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx->pipes_ctx->thread;
			to = pipes_handle_findname(name, thCtx->global->handle_mgr);
			if (to == 0)  // dest service not found, send to sys-service of cur-thread
				{
					struct pipes_worker_thread_context* wkCtx = (struct pipes_worker_thread_context*)ctx->pipes_ctx->thread;
					to = wkCtx->sys_service->handle;
				}
		}
	else  // use addr
		{
			to = luaL_checkinteger(L, from + 0);
			to = pipes_handle_local2id(to, ctx->pipes_ctx->thread->global->harbor);   // localid
		}
	int session = luaL_checkinteger(L, from + 1);
	int type = luaL_checkinteger(L, from + 2);
	void* ptr = NULL;
	size_t sz = 0;
	if (type == PMSG_TYPE_DATA_STR || type == PMSG_TYPE_RET_ERROR)
	{
		const char* str = lua_tolstring(L, from + 3, &sz);
		if (str)
		{
			ptr = luapps_malloc(sz);
			memcpy(ptr, str, sz);
			*(char*)(ptr + sz) = 0;
		}
		else
		{
			return luaL_error(L, "no string specify");
		}
	}
	else
	{
		ptr = lua_touserdata(L, from + 3);
		if (ptr)
		{
			sz = luaL_checkinteger(L, from + 4);
		}	
	}
	//
	pipes_api_send(ctx->pipes_ctx, to, type, session, ptr, sz);
	
	lua_pushboolean(L, 1);
	return 1;
}
static int name(lua_State* L, struct lua_service_context* ctx, int from)
{
	int ret = -1;
	lua_Integer addr = 0;
	do
	{
		size_t sz = 0;
		const char* name = luaL_checklstring(L, from + 0, &sz);
		if (name == NULL)
		{
			return luaL_error(L, "name, no name specify");
		}
		if (lua_type(L, from + 1) == LUA_TNUMBER) // number
			{
				addr = luaL_checkinteger(L, from + 1);
				addr = pipes_handle_local2id(addr, ctx->pipes_ctx->thread->global->harbor); // localid
			}
		else
		{
			return luaL_error(L, "name, no addr specify");
		}
		//
		struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx->pipes_ctx->thread;
		void * service = pipes_handle_grab(addr, thCtx->global->handle_mgr);
		if (service == NULL)
		{
			ret = 1;
			break;
			//return luaL_error(L, "name, dest addr not exist: %ld", addr);
		}
		const char* nameAdded = pipes_handle_namehandle(addr, name, thCtx->global->handle_mgr);
		if (nameAdded == NULL)  // name exist
			{
				ret = 2;
				break;
				//return luaL_error(L, "name, name already exist: %s", name);
			}
		ret = 0;
	} while (0);
	if (ret == 0)  // name succ
		{
			lua_pushboolean(L, 1);
			lua_pushnil(L);
		}
	else  // name failed
	{
		lua_pushboolean(L, 0);
		lua_pushinteger(L, ret);
	}
	return 2;
}
static int l_send(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	// cmd
	int cmd = luaL_checkinteger(L, 1);
	switch (cmd)
	{
	case LPPS_CMD_SEND : {
			return send(L, ctx, 2);	
		}
	case LPPS_CMD_TIMEOUT: {
			return timeout(L, ctx, 2);
		}
	case LPPS_CMD_NEWSERVICE: {  // new service
			return newservice(L, ctx, 2);
		}
	case LPPS_CMD_NAME: {
			return name(L, ctx, 2);
		}
	default:
		luaL_error(L, "send, unknown cmd: %d", cmd);
		break;
	}
	return 0;
}

// unpack
struct unpack_ptr_iter
{
	lua_State* L;
	int cnt;
}
;
static void on_unpack_ptr_iter(void* ptr, void* udata)
{
	struct unpack_ptr_iter* iter = (struct unpack_ptr_iter*)udata;
	lua_State* L = iter->L;
	int t = lua_getglobal(L, "LPPS_UNPACK_PTR_ITER");
	if (t != LUA_TFUNCTION)
	{
		luaL_error(L, "LPPS_UNPACK_PTR_ITER not found");	
		return;
	}
	lua_pushlightuserdata(L, ptr);
	lua_pushinteger(L, ++iter->cnt);
	int ok = lua_pcall(L, 2, 0, 0);
	if (ok != LUA_OK)
	{
		luaL_error(L, "call LPPS_UNPACK_PTR_ITER error: %s", lua_tostring(L, -1));
	}
}
static int l_luaunpack(lua_State* L)
{
	void* data = lua_touserdata(L, 1);
	lua_Integer sz = luaL_checkinteger(L, 2);
	if (data == NULL)
	{
		//return luaL_error(L, "unpack data is null");
		return 0;
	}
	int argNum = 0;
	if (lua_isfunction(L, 3))  // has ptr cb
		{
			struct unpack_ptr_iter iter;
			iter.L = L;
			iter.cnt = 0;
			argNum = lua_seri_unpack(L, data, sz, on_unpack_ptr_iter, &iter);
		}
	else
	{
		argNum = lua_seri_unpack(L, data, sz, NULL, NULL);
	}
	return argNum;
}


static int l_time(lua_State* L)
{
	struct timeval val;
	pipes_time_now(&val);
	uint64_t nowMs = pipes_time_toms(&val);
	lua_pushinteger(L, nowMs);
	return 1;
}

static int l_rawtostr(lua_State* L)
{
	void* data = lua_touserdata(L, 1);
	if (data)
	{      
		int isNum = 0;
		lua_Integer sz = lua_tointegerx(L, 2, &isNum);
		if (isNum)
		{
			int off = lua_tointegerx(L, 3, &isNum);
			if (isNum)  // specify off
			{
				lua_pushlstring(L, data + off, sz);	
			}
			else
			{
				lua_pushlstring(L, data, sz);
			}
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

static int l_error(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	//
	size_t len = 0;
	const char* msg = luaL_checklstring(L, 1, &len);
	// raise an error in main-thread
	luaL_error(ctx->L, msg);
	return 0;
}
//


static int l_stat(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx->pipes_ctx->thread;
	int type = luaL_checkinteger(L, 1);
	switch (type)
	{
	case LPPS_STAT_ID: {
		lua_pushinteger(L, pipes_handle_id2local(ctx->pipes_ctx->handle));
		lua_pushinteger(L, ctx->pipes_ctx->thread->idx);
		return 2;
	}
	case LPPS_STAT_MEM: {
		lua_pushinteger(L, ctx->mem);
		return 1;
	}
	case LPPS_STAT_MQLEN: {
		int msgNum = 0;
		if (ctx->pipes_ctx->queue)
		{
			msgNum = pipes_mq_size_unsafe(ctx->pipes_ctx->queue);
		}
		lua_pushinteger(L, msgNum);
		return 1;
	}
	case LPPS_STAT_MESSAGE: {
		lua_pushinteger(L, ctx->pipes_ctx->msg_proc);
		return 1;
	}
	case LPPS_STAT_MEMTH: {
		lua_pushinteger(L, ctx->pipes_ctx->thread->mem);
		return 1;
	}
	case LPPS_STAT_SERVICENUM: {
		int num = 0;
		if (lua_isinteger(L, 2))  // specify thread
		{
			int th = luaL_checkinteger(L, 2);
			struct pipes_global_context* g = ctx->pipes_ctx->thread->global;
			if (th < 0 || th >= g->worker_thread_num)
			{
				return luaL_error(L, "invalid thread: %d", th);
			}
			num = g->threads[th]->service_num;
		}
		else  // query all
		{
			struct pipes_global_context* g = ctx->pipes_ctx->thread->global;
			int i;
			for (i=0; i<g->worker_thread_num; ++i)
			{
				num += g->threads[i]->service_num;
			}
		}
		lua_pushinteger(L, num);
		return 1;
	}
	default: {
		return luaL_error(L, "invalid stat type: %d", type);
	}
	}
	return 1;
}

static int l_env(lua_State* L)
{
	const char* key = luaL_checkstring(L, 1);
	if (key == NULL)
	{
		return luaL_error(L, "env, key not specify");
	}
	const char * val = lua_pipes_getenv(key);
	lua_pushstring(L, val);
	return 1;
}
static int l_shutdown(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	pipes_api_shutdown(ctx->pipes_ctx);
	return 0;
}

int luapps_api_openlib(lua_State* L)
{
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		
		{ "malloc", l_malloc},
		{ "retain", l_retain},
		//
		{ "time", l_time},
		{ "send", l_send},
		{ "dispatch", l_dispatch},
		{ "luapack", l_luapack},
		{ "luaunpack", l_luaunpack},
		{ "rawtostr", l_rawtostr},
		{ "free", l_free},
		{ "error", l_error},
		{ "exit", l_exit},
		{ "stat", l_stat},
		{ "env", l_env},
		{ "shutdown", l_shutdown},
		//
		{ NULL, NULL },
	};
	// 
	lua_createtable(L, 0, sizeof(l) / sizeof(l[0]) - 2);
	// 
	lua_getfield(L, LUA_REGISTRYINDEX, PIPES_CONTEXT);
	struct lua_service_context *ctx = lua_touserdata(L, -1);
	if (ctx == NULL) {
		return luaL_error(L, "lua-service has not initialized");
	}
	luaL_setfuncs(L, l, 1);
	
	return 1;
}

