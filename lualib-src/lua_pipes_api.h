#ifndef LUA_PIPES_API_H
#define LUA_PIPES_API_H

#include "lua.h"

#define LPPS_CMD_NEWSERVICE 1
#define LPPS_CMD_TIMEOUT 2
#define LPPS_CMD_SEND 3
#define LPPS_CMD_NAME 4

//
#define LPPS_STAT_ID 1
#define LPPS_STAT_MEM 2
#define LPPS_STAT_MQLEN 3
#define LPPS_STAT_MESSAGE 4
#define LPPS_STAT_MEMTH 5
#define LPPS_STAT_SERVICENUM 6

int luapps_api_openlib(lua_State* L);


#endif 
