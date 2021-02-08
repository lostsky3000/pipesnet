
#include "lua_pipes_json.h"
#include "lua_pipes_core.h"
#include "cJSON.h"
#include "lualib.h"
#include "lauxlib.h"

#define ARRAY_FLAG "_ArR"

struct seri_ctx
{
	cJSON* root;
};

static int seri_one(struct seri_ctx* ctx, lua_State* L, int idx, cJSON* wrap, const char* key);

static int write_table(struct seri_ctx* ctx, lua_State* L, int idx, cJSON* wrap, const char* key)
{
	size_t arrLen = lua_rawlen(L, idx);
	if (ctx->root == NULL)  // is root node
	{
		if (arrLen > 0)  // root node cant be array
		{
			return 2;
		}
	}
	if (arrLen > 0)  //array
	{	
		cJSON* jArr = cJSON_CreateArray();
		int i, ret = 0;
		for (i = 1; i <= arrLen; ++i)
		{
			lua_rawgeti(L, idx, i);
			ret = seri_one(ctx, L, -1, jArr, NULL);
			lua_pop(L, 1);
			if (ret != 0)  // has error
			{
				break;
			}
		}
		if (ret != 0) // has error
		{
			cJSON_Delete(jArr);
		}
		else  // succ
		{
			if (key)
			{
				cJSON_AddItemToObject(wrap, key, jArr);
			}
			else
			{
				cJSON_AddItemToArray(wrap, jArr);
			}
		}
		return ret;
	}
	else  // object
	{
		int type = lua_getfield(L, idx, ARRAY_FLAG);
		lua_pop(L, 1);
		if (type != LUA_TNIL)  // is from json array
		{
			cJSON* jArr = cJSON_CreateArray();
			if (key)
			{
				cJSON_AddItemToObject(wrap, key, jArr);
			}
			else
			{
				cJSON_AddItemToArray(wrap, jArr);
			}
			return 0;
		}
		int ret = 0;
		cJSON* jObj = cJSON_CreateObject();
		if (ctx->root == NULL)
		{
			ctx->root = jObj;
		}
		// check hash
		if (idx < 0)
		{
			idx = lua_gettop(L) + idx + 1;
		}
		lua_pushnil(L);
		while (lua_next(L, idx) != 0)
		{
			if (lua_isstring(L, -2))  // key is string
			{
				const char* key = lua_tostring(L, -2);
				ret = seri_one(ctx, L, -1, jObj, key);
				if (ret == 0)  // succ
				{
					lua_pop(L, 1);
				}
				else  // error
				{
					lua_pop(L, 2);
					break;
				}
			}
			else  // invalid key
			{
				lua_pop(L, 2);
				ret = 4;
				break;
			}
		}
		if (ret != 0) // has error
		{
			if (ctx->root == jObj)
			{
				ctx->root = NULL;
			}
			cJSON_Delete(jObj);
		}
		else // succ
		{
			if (wrap)
			{
				if (key)
				{
					cJSON_AddItemToObject(wrap, key, jObj);
				}
				else
				{
					cJSON_AddItemToArray(wrap, jObj);
				}
			}
		}
		return ret;
	}
}
static int write_str(struct seri_ctx* ctx, lua_State*L, int idx, cJSON* wrap, const char* key)
{
	const char* val = lua_tostring(L, idx);
	if (key)
	{
		cJSON_AddStringToObject(wrap, key, val);
	}
	else
	{
		cJSON_AddItemToArray(wrap, cJSON_CreateString(val));
	}
	return 0;
}
static int write_num(struct seri_ctx* ctx, lua_State*L, int idx, cJSON* wrap, const char* key)
{
	if (lua_isinteger(L, idx))  // try integer
	{
		lua_Integer val = lua_tointeger(L, idx);
		if (val >= INT_MIN && val <= INT_MAX)
		{
			if (key)
			{
				cJSON_AddNumberToObject(wrap, key, val);
			}
			else
			{
				cJSON_AddItemToArray(wrap, cJSON_CreateNumber(val));
			}
			return 0;
		}
	}
	// treat as float
	lua_Number val = lua_tonumber(L, idx);
	if(key)
	{
		cJSON_AddNumberToObject(wrap, key, val);
	}
	else
	{
		cJSON_AddItemToArray(wrap, cJSON_CreateNumber(val));
	}
	return 0;
}
static int write_bool(struct seri_ctx* ctx, lua_State*L, int idx, cJSON* wrap, const char* key)
{
	int b = lua_toboolean(L, idx);
	if (key)
	{
		cJSON_AddBoolToObject(wrap, key, b);
	}
	else
	{
		cJSON_AddItemToArray(wrap, cJSON_CreateBool(b));
	}
	return 0;
}
static int seri_one(struct seri_ctx* ctx, lua_State* L, int idx, cJSON* wrap, const char* key)
{
	int type = lua_type(L, idx);
	switch (type)
	{
	case LUA_TSTRING: {
		return write_str(ctx, L, idx, wrap, key);
	}
	case LUA_TNUMBER: {
		return write_num(ctx, L, idx, wrap, key);	
	}
	case LUA_TTABLE:
	{
		return write_table(ctx, L, idx, wrap, key);
	}
	case LUA_TBOOLEAN:
	case LUA_TNIL:
		{
			return write_bool(ctx, L, idx, wrap, key);
		}
	default:
		return 1;
	}
	return -1;
}
static int l_seri(lua_State* L)
{
	int type = lua_type(L, 1);
	if (type != LUA_TTABLE)
	{
		return luaL_error(L, "seri obj-type must be table");
	}
	struct seri_ctx sctx;
	sctx.root = NULL;
	//
	int ret = seri_one(&sctx, L, 1, NULL, NULL);
	//
	if(ret == 0)  // succ
	{
		const char* str = cJSON_PrintUnformatted(sctx.root);
		cJSON_Delete(sctx.root);
		lua_pushstring(L, str);
		cJSON_free((void*)str);
		return 1;
	}
	else
	{
		if (sctx.root)
		{
			cJSON_Delete(sctx.root);
		}
		char err[32];
		sprintf(err, "seri failed: %d", ret);
		lua_pushnil(L);
		lua_pushstring(L, err);
		return 2;
	}
}

//
static int deseri_obj(cJSON* obj, lua_State* L, int depth)
{
	if (cJSON_IsObject(obj))
	{
		luaL_checkstack(L, LUA_MINSTACK, NULL);
		lua_createtable(L, 0, 0);
		cJSON* item = obj->child;
		while (item != NULL)
		{
			lua_pushstring(L, item->string);  // key
			deseri_obj(item, L, depth + 1);   // value
			lua_rawset(L, -3);
			//
			item = item->next;
		}
	}
	else if (cJSON_IsArray(obj)) // is array
	{
		int len = cJSON_GetArraySize(obj);
		luaL_checkstack(L, LUA_MINSTACK, NULL);
		lua_createtable(L, len, 1);
		cJSON* item = obj->child;
		int idx = 0;
		while (item != NULL)
		{
			deseri_obj(item, L, depth + 1);
			lua_rawseti(L, -2, ++idx);
			//
			item = item->next;
		}
		// mark table is array
		lua_pushstring(L, ARRAY_FLAG);
		lua_pushboolean(L, 1);
		lua_rawset(L, -3);
	}
	else
	{
		if (cJSON_IsNumber(obj))
		{
			if ((double)obj->valueint == obj->valuedouble)  // int
			{
				lua_pushinteger(L, obj->valueint);
			}
			else
			{
				lua_pushnumber(L, obj->valuedouble);
			}
		}
		else if (cJSON_IsString(obj))
		{
			lua_pushstring(L, obj->valuestring);
		}
		else if (cJSON_IsBool(obj))
		{
			lua_pushboolean(L, cJSON_IsTrue(obj) ? 1 : 0);
		}
	}
	return 0;
}
static int deseri(const char* str, lua_State* L)
{
	cJSON* root = cJSON_Parse(str);
	if (root == NULL)  // parse failed
	{
		lua_pushnil(L);
		lua_pushstring(L, "json format invalid");
		return 2;
	}
	deseri_obj(root, L, 0);
	cJSON_Delete(root);
	return 1;
}

static int l_deseri(lua_State* L)
{
	const char* str = luaL_checkstring(L, 1);
	return deseri(str, L);
}

int luapps_json_openlib(lua_State* L)
{
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		{ "deseri", l_deseri },
		{ "seri", l_seri},
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
