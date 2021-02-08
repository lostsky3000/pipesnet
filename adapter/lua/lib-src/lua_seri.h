#ifndef LUA_SERI_H
#define LUA_SERI_H

#include <stdlib.h>
#include "lua.h"



typedef void*(*fn_lua_seri_realloc)(
	void* ptr, size_t szOldData,
	size_t szNewPrefer, size_t szNewMin, size_t* szNewActual, void* udata);
typedef void(*fn_lua_seri_free)(void* ptr, void* udata);


typedef void(*fn_lua_seri_ptr_iter)(void * ptr, void* udata);

void* lua_seri_pack(
	lua_State* L, int from, size_t* szOut, 
	void* buf, size_t bufCap, void*udata,
	fn_lua_seri_realloc fnRealloc, fn_lua_seri_free fnFree,
	fn_lua_seri_ptr_iter fnPtrIter);


typedef void(*fn_lua_unpack_ptr_iter)(void* ptr, void* udata);
int lua_seri_unpack(lua_State*L, void*buf, size_t szBuf, fn_lua_unpack_ptr_iter fnPtrIter, void* udata);


#endif // !LUA_SERI_H



