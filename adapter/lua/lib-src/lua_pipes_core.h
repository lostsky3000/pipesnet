#ifndef LUA_PIPES_CORE_H
#define LUA_PIPES_CORE_H

#include <stdint.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define PIPES_CONTEXT "PIPES_CONTEXT"
#define LUA_OPENLIB_FUNC "OPEN_PIPES_C_LIB"
#define LUA_OPENSOCK_FUNC "OPEN_PIPES_SOCK_LIB"
#define LUA_OPENJSON_FUNC "OPEN_PIPES_JSON_LIB"

#define LMSG_TYPE_LUA PMSG_TYPE_CUSTOM_BEGIN

struct pipes_service_context;
struct lua_service_context
{
	int fn_msg_cb;
	uint32_t param_size;
	int64_t mem;
	lua_State* L;
	void* param;
	char* src_path;
	struct pipes_service_context* pipes_ctx; 
};

uint64_t luapps_create_service(
	struct lua_service_context* caller, 
	int toThread,
	int session,
	const char* name,
	const char* srcPath,
	int paramType,
	void* param, 
	size_t paramSize);

//
void* luapps_on_boot_create(int curThread);
int luapps_on_service_init(void* adapter, struct pipes_service_context* service);
void luapps_on_service_exit(void* adapter);
int luapps_on_message(
	struct pipes_service_context* ctx,
	int type,
	int session,
	uint64_t source,
	void* data,
	uint32_t size);
void luapps_destroy_adapter(void* adapter);

void* luapps_copy_string(const char* ori, size_t* sz);

void luapps_on_sys_init();

void luapps_add_thread_mem(struct lua_service_context* ctx, int dlt);

#endif




