#ifndef LUA_PIPES_SOCKET_H
#define LUA_PIPES_SOCKET_H

#include "lua.h"

#define LPPS_NET_CMD_LISTEN 1
#define LPPS_NET_CMD_SESSION_START 2
#define LPPS_NET_CMD_CLOSE 3
#define LPPS_NET_CMD_SEND 4
#define LPPS_NET_CMD_OPEN 5

int luapps_socket_openlib(lua_State* L);


#endif 
