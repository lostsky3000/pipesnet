

#include "lua_pipes_socket.h"
#include "lua_pipes_core.h"
#include "lualib.h"
#include "lauxlib.h"

#include "pipes_tcp.h"
#include "pipes_socket.h"
#include "pipes_socket_thread.h"
#include "lua_pipes_malloc.h"

#include <string.h>

#define IDX_SEP_FLAG 0

struct lua_sock_session
{
	int mem;
	int unread;
	struct net_readblock_head* buf_head;
	struct net_readblock_head* buf_tail;
};

static int netunpack_listen(lua_State* L, uint32_t id, void*data)
{
	struct net_tcp_listen_ret* ret = (struct net_tcp_listen_ret*)data;
	if (ret->succ)
	{
		lua_pushinteger(L, id);
		return 1;
	}
	else
	{
		lua_pushnil(L);
		lua_pushstring(L, "listen failed");
		return 2;
	}
}
static int netunpack_accept(lua_State* L, uint32_t id, void*data)
{
	struct net_tcp_accept* ret = (struct net_tcp_accept*)data;
	lua_pushinteger(L, id);
	lua_pushinteger(L, ret->conn_id);
	lua_pushstring(L, ret->host);
	lua_pushinteger(L, ret->port);
	return 4;
}
static int netunpack_connect(lua_State* L, uint32_t id, void*data)
{
	struct net_tcp_connect_ret* ret = (struct net_tcp_connect_ret*)data;
	if (id > 0)  // conn succ
		{
			lua_pushinteger(L, id);
			return 1;
		}
	else
	{
		lua_pushnil(L);
		/*
		char err[64];
		sprintf(err, "conn failed, %d", ret->ret);
		lua_pushstring(L, err);
		*/
		lua_pushinteger(L, ret->ret);
		return 2;
	}
}
static int l_netunpack(lua_State* L)
{
	void* raw = lua_touserdata(L, 1);
	if (raw == NULL)
	{
		return luaL_error(L, "netunpack, no data");
	}
	size_t szRaw = luaL_checkinteger(L, 2);
	int cmd = 0;
	uint32_t id = 0;
	size_t szPayload = 0;
	void * payload = unwrap_net_msg(raw, szRaw, &cmd, &id, &szPayload);
	switch (cmd)
	{
	case NET_CMD_TCP_ACCEPT: {
			return netunpack_accept(L, id, payload);
		}
	case NET_CMD_TCP_CONNECT: {
			return netunpack_connect(L, id, payload);
		}
	case NET_CMD_TCP_LISTEN: {
			return netunpack_listen(L, id, payload);
		}
	default: {
			return luaL_error(L, "netunpack, unknown cmd: %d", cmd);
		}
	}
	return 0;
}
static int l_nethead(lua_State* L)
{
	void* data = lua_touserdata(L, 1);
	if (data == NULL)
	{
		return luaL_error(L, "netcmd, no data");
	}
	int szRaw = luaL_checkinteger(L, 2);
	uint32_t id = 0;
	int cmd = gain_net_msg_head(data, &id);
	lua_pushinteger(L, cmd);
	lua_pushinteger(L, id);
	return 2;
}
//
static lua_Integer check_fieldinteger(lua_State* L, int idxTable, const char* key)
{
	lua_getfield(L, idxTable, key);
	lua_Integer ret = luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	return ret;
}
static const char* check_fieldstring(lua_State* L, int idxTable, const char* key)
{
	lua_getfield(L, idxTable, key);
	const char* ret = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	return ret;
}
static const char* opt_fieldstring(lua_State* L, int idxTable, const char* key, const char* def)
{
	int type = lua_getfield(L, idxTable, key);
	if (type == LUA_TSTRING) // string
		{
			const char* ret = lua_tostring(L, -1);
			lua_pop(L, 1);
			return ret;
		}
	lua_pop(L, 1);
	return def;
}
static lua_Integer opt_fieldinteger(lua_State* L, int idxTable, const char* key, lua_Integer def)
{
	lua_getfield(L, idxTable, key);
	if (lua_isinteger(L, -1)) 
	{
		lua_Integer ret = lua_tointeger(L, -1);
		lua_pop(L, 1);
		return ret;
	}
	lua_pop(L, 1);
	return def;
}
static void parse_tcp_decoder(lua_State* L, int idxTable, struct tcp_decoder* dec)
{
	int decType = TCP_DECODE_RAW;   // default type
	int decVal = 0;
	int fieldDepth = 0;
	int maxlength = READ_PACK_MAX_SIZE;
	do
	{
		int type = lua_getfield(L, idxTable, "decoder");
		++fieldDepth;
		if (type != LUA_TTABLE)
		{
			break;
		}   
		//maxblocksize = opt_fieldinteger(L, -1, "maxblocksize", maxblocksize);
		//
		lua_getfield(L, -1, "maxlength");
		++fieldDepth;
		if (lua_isinteger(L, -1))
		{
			int len = lua_tointeger(L, -1);
			if (len < 1)
			{
				luaL_error(L, "invalid decoder maxlength: %d", len);
			}
			maxlength = len;
		}
		lua_pop(L, 1); 
		--fieldDepth;
		//
		type = lua_getfield(L, -1, "type");
		++fieldDepth;
		if (type != LUA_TSTRING)
		{
			luaL_error(L, "decoder type expected");
		}
		const char* sType = lua_tostring(L, -1);
		lua_pop(L, 1); 
		--fieldDepth;
		if (strcmp(sType, "fieldlength") == 0)
		{
			decType = TCP_DECODE_FIELD_LENGTH;
			int lenBytes = opt_fieldinteger(L, -1, "lengthbytes", 2);
			if (lenBytes != 2 && decVal != 4)
			{
				luaL_error(L, "unsupport lenbytes: %d", lenBytes);
			}
			decVal = lenBytes;
		}
		else 
		{
			luaL_error(L, "unknown decoder type: %s", sType);
		}
	} while (0);
	int i;
	for (i = 0; i < fieldDepth; ++i)
	{
		lua_pop(L, 1);	
	}
	if (maxlength < 1)
	{
		luaL_error(L, "maxpacklength invalid: %d", maxlength);
	}
	dec->type = decType;
	dec->val1 = decVal;
	dec->maxlength = maxlength;
	return;
}
static int do_tcp_listen(lua_State*L, struct lua_service_context* ctx, int from)
{
	int tbIdx = from + 0;
	luaL_checktype(L, tbIdx, LUA_TTABLE);
	int  session = luaL_checkinteger(L, from + 1);
	//
	int port = check_fieldinteger(L, tbIdx, "port");
	const char* host = opt_fieldstring(L, tbIdx, "host", "0.0.0.0");
	int backlog = opt_fieldinteger(L, tbIdx, "backlog", 128);
	//
	struct pipes_tcp_server_cfg cfg;
	parse_tcp_decoder(L, tbIdx, &cfg.decoder);
	cfg.port = port;
	cfg.backlog = backlog;
	strcpy(cfg.host, host);
	pipes_api_tcp_listen(ctx->pipes_ctx, session, &cfg);
	return 0;
}
static int tcp_connect(lua_State*L, struct lua_service_context* ctx, int from)
{
	int tbIdx = from + 0;
	luaL_checktype(L, tbIdx, LUA_TTABLE);
	int  session = luaL_checkinteger(L, from + 1);
	//
	int port = check_fieldinteger(L, tbIdx, "port");
	const char* host = check_fieldstring(L, tbIdx, "host");
	int timeout = opt_fieldinteger(L, tbIdx, "timeout", 30000);
	//
	struct pipes_tcp_client_cfg cfg;
	parse_tcp_decoder(L, tbIdx, &cfg.decoder);
	strcpy(cfg.host, host);
	cfg.port = port;
	cfg.conn_timeout = timeout;
	pipes_api_tcp_connect(ctx->pipes_ctx, session, &cfg);
	return 0;
}
static int tcp_session_start(lua_State*L, struct lua_service_context* ctx, int from)
{
	uint32_t id = luaL_checkinteger(L, from + 0);
	uint64_t source = luaL_checkinteger(L, from + 1);
	int ret = pipes_api_tcp_session_start(ctx->pipes_ctx, id, source);
	lua_pushboolean(L, 1);
	return 1;
}
static int tcp_session_close(lua_State*L, struct lua_service_context* ctx, int from)
{
	uint32_t id = luaL_checkinteger(L, from + 0);
	pipes_api_tcp_close(ctx->pipes_ctx, id);
	return 0;
}
static int tcp_send(lua_State*L, struct lua_service_context* ctx, int from)
{
	uint32_t id = luaL_checkinteger(L, from);
	int type = lua_type(L, from + 1);
	if (type == LUA_TNIL)
	{
		return luaL_error(L, "tcp_send error, data is nil");
	}
	int ret = 0;
	if (type == LUA_TSTRING)  // lua-string
		{
			size_t sz = 0;
			const char* str = luaL_tolstring(L, from + 1, &sz);
			ret = pipes_net_tcp_send(ctx->pipes_ctx->thread->global->net_ctx, id, (void*)str, 0, sz, 1);
		}
	else
	{
		return luaL_error(L, "tcp send error, invalid data type: %d", type);
	}
	if (ret > 0) // succ
		{
			lua_pushboolean(L, 1);
		}
	else  //failed
		{
			lua_pushboolean(L, 0);
		}
	return 1;
}

//
static void push_sock_buf(struct lua_service_context*ctx, struct lua_sock_session* sock, struct net_readblock_head* buf)
{
	if (sock->buf_tail != NULL)
	{
		sock->buf_tail->next = buf;
	}
	else
	{
		sock->buf_head = buf;
	}
	sock->buf_tail = buf;
	buf->next = NULL;
	//
	sock->mem += buf->size;
	luapps_add_thread_mem(ctx, buf->size); // update thread mem
}
static struct net_readblock_head* pop_sock_buf(struct lua_service_context*ctx, struct lua_sock_session* sock)
{
	if (sock->buf_head != NULL)
	{
		struct net_readblock_head* ret = sock->buf_head;
		sock->buf_head = sock->buf_head->next;
		if (sock->buf_head == NULL)
		{
			sock->buf_tail = NULL;
		}
		ret->next = NULL;
		//
		sock->mem -= ret->size;
		luapps_add_thread_mem(ctx, -ret->size);   // update thread mem
		return ret;
	}
	return NULL;
} 
static int l_newsock(lua_State* L)
{
	size_t sz = sizeof(struct lua_sock_session);
	struct lua_sock_session* sock = pipes_malloc(sz);
	memset(sock, 0, sz);
	lua_pushlightuserdata(L, sock);
	return 1;
}
static int l_freesock(lua_State* L)
{
	struct lua_sock_session* sock = lua_touserdata(L, 1);
	if (sock == NULL)
	{
		luaL_error(L, "no sock session");
	}
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	struct net_readblock_head* buf = NULL;
	while ((buf = pop_sock_buf(ctx, sock)) != NULL)
	{
		luapps_free(buf, 1);
	}
	pipes_free(sock);
	return 0;
}
static int l_pushsockbuf(lua_State* L)
{
	struct lua_sock_session* sock = lua_touserdata(L, 1);
	if (sock == NULL)
	{
		luaL_error(L, "no sock session");
	}
	struct net_readblock_head* block = lua_touserdata(L, 2);
	if (block == NULL)
	{
		luaL_error(L, "no sock buf");
	}
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	block->ridx = 0;   // init read-idx
	block->meta[IDX_SEP_FLAG] = -1;   // init readsep/line sep flag
	push_sock_buf(ctx, sock, block);
	sock->unread += block->size;   // add unread size
	return 0;
}
static int _readsockstr(lua_State* L, struct lua_sock_session* sock, size_t szReq)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	if (szReq)   // size required
		{
			if (szReq > sock->unread) // not enough data
				{
					return 0;
				} 
			int reqLeft = szReq;
			luaL_Buffer buf;
			luaL_buffinitsize(L, &buf, szReq); 
			struct net_readblock_head* block = NULL;
			while (reqLeft > 0 && (block = sock->buf_head) != NULL)   // get head buf
				{
					int blockUnread = block->size - block->ridx;
					if (blockUnread <= reqLeft)   // consume whole block
						{
							luaL_addlstring(&buf, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, blockUnread);
							pop_sock_buf(ctx, sock);
							luapps_free(block, 1);
							//
							reqLeft -= blockUnread;
							sock->unread -= blockUnread;
						}
					else // consume part of block
						{
							luaL_addlstring(&buf, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, reqLeft);
							block->ridx += reqLeft;
							//
							sock->unread -= reqLeft;
							reqLeft = 0;
							break;
						}
				}
			//luaL_pushresultsize(&buf, szReq + 1);
			luaL_pushresult(&buf);
			lua_pushinteger(L, szReq);
			return 2;
		}
	else  // size not required, return one block
		{
			struct net_readblock_head* block = pop_sock_buf(ctx, sock);
			int left = block->size - block->ridx;
			sock->unread -= left;      // update pack size
			//
			lua_pushlstring(L, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, left);
			lua_pushinteger(L, left);
			//
			luapps_free(block, 1);       // free buf
			return 2;
		}
	return 0;
}
static int read_sockstr(lua_State* L, struct lua_sock_session* sock, int from)
{
	if (sock->unread < 1)  // empty
		{
			return 0;
		}
	int isNum;
	size_t szReq = lua_tointegerx(L, from, &isNum);
	if (isNum && szReq < 1) // is num but invalid
		{
			szReq = 0;
		}
	return _readsockstr(L, sock, szReq);
}
static int read_socktobuf(struct lua_service_context*ctx, struct lua_sock_session* sock, int szReq, unsigned char* buf)
{
	if (szReq <= sock->unread)  // left data enough
		{
			int reqLeft = szReq;
			int ridx = 0;
			struct net_readblock_head* block = NULL;
			while (reqLeft > 0 && (block = sock->buf_head) != NULL)
			{
				int szRead = block->size - block->ridx;
				if (szRead <= reqLeft)  // read whole block
					{
						memcpy(buf + ridx, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, szRead);
						pop_sock_buf(ctx, sock);
						luapps_free(block, 1);
					}
				else // read part of block
					{
						szRead -= reqLeft;
						memcpy(buf + ridx, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, szRead);
						block->ridx += szRead;
					}
				reqLeft -= szRead;
				ridx += szRead;
				sock->unread -= szRead;
			}
			return 1;
		}
	return 0;
}
static int read_sockint(lua_State* L, struct lua_sock_session* sock, int from, struct pipes_global_context* g)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	// lenbytes, flag('u'=unsigned, 'l'=little endian)
	unsigned char lenbytes = luaL_checkinteger(L, from);
	// check flag
	char isSigned = 1;  // is signed int
	char isBE = 1;   // is big-endian
	size_t flagLen = 0;
	const char* flag = lua_tolstring(L, from + 1, &flagLen);
	if (flag)  // has set flag
		{
			char c1 = flag[0];
			if (flagLen == 1)
			{
				if (c1 == 'u')
				{
					isSigned = 0;
				}
				else if (c1 == 'l')
				{
					isBE = 0;
				}
				else 
				{
					return luaL_error(L, "readint, invalid flag: %c", c1);
				}
			}
			else if (flagLen == 2)
			{
				char c2 = flag[1];
				if ((c1 == 'u' && c2 == 'l') || (c1 == 'l'&&c2 == 'u'))
				{
					isSigned = 0;
					isBE = 0;
				}
				else
				{
					return luaL_error(L, "read int, invalid flag: %s", flag);
				}
			}
			else
			{
				return luaL_error(L, "readint, invalid flag length:%d", flagLen);
			}
		}
	if (lenbytes == 4)
	{
		unsigned char buf[4];
		if (read_socktobuf(ctx, sock, 4, buf))   // read succ
			{
				int reverse = !((isBE && g->is_bigendian) || (!isBE && !g->is_bigendian));
				if (isSigned)
				{
					if (reverse)
					{
						if (isBE)
						{
							int ret = (char)buf[0];
							lua_pushinteger(L, (((((ret << 8) | buf[1]) << 8) | buf[2]) << 8) | buf[3]);
						}
						else 
						{
							int ret = (char)buf[3];
							lua_pushinteger(L, (((((ret << 8) | buf[2]) << 8) | buf[1]) << 8) | buf[0]);	
						}
					}
					else 
					{
						lua_pushinteger(L, *(int*)buf);
					}
				}
				else  // unsigned
					{
						if (reverse)
						{
							if (isBE)
							{
								uint32_t ret = buf[0];
								lua_pushinteger(L, (((((ret << 8) | buf[1]) << 8) | buf[2]) << 8) | buf[3]);
							}
							else 
							{
								uint32_t ret = buf[3];
								lua_pushinteger(L, (((((ret << 8) | buf[2]) << 8) | buf[1]) << 8) | buf[0]);	
							}
						}
						else 
						{
							lua_pushinteger(L, *(uint32_t*)buf);
						}
					}
				return 1;
			}
	}
	else if (lenbytes == 2)
	{
		unsigned char buf[2];
		if (read_socktobuf(ctx, sock, 2, buf))  // read succ
			{
				int reverse = !((isBE && g->is_bigendian) || (!isBE && !g->is_bigendian));
				if (isSigned)
				{
					if (reverse)
					{
						if (isBE)
						{
							int16_t ret = (char)buf[0];
							lua_pushinteger(L, (ret << 8) | buf[1]);
						}
						else 
						{
							int16_t ret = (char)buf[1];
							lua_pushinteger(L, (ret << 8) | buf[0]);	
						}
					}
					else 
					{
						lua_pushinteger(L, *(int16_t*)buf);
					}
				}
				else  // unsigned
					{
						if (reverse)
						{
							if (isBE)
							{
								uint16_t ret = buf[0];
								lua_pushinteger(L, (ret << 8) | buf[1]);
							}
							else 
							{
								uint16_t ret = buf[1];
								lua_pushinteger(L, (ret << 8) | buf[0]);	
							}
						}
						else 
						{
							lua_pushinteger(L, *(uint16_t*)buf);
						}
					}
				return 1;
			}
	}
	else if (lenbytes == 1)
	{
		unsigned char buf[1];
		if (read_socktobuf(ctx, sock, 1, buf)) // read succ
			{
				lua_pushinteger(L, isSigned ? (char)buf[0] : buf[0]);
				return 1;
			}
	}
	else if (lenbytes == 8)
	{
		unsigned char buf[8];
		if (read_socktobuf(ctx, sock, 8, buf))  // read succ
			{
				int reverse = !((isBE && g->is_bigendian) || (!isBE && !g->is_bigendian));
				if (reverse)
				{
					int i;
					if (isBE)
					{
						int64_t ret = (char)buf[0];
						for (i = 1; i < 8; ++i)
						{
							ret = (ret << 8) | buf[i];
						}
						lua_pushinteger(L, ret);
					}
					else 
					{
						int64_t ret = (char)buf[7];
						for (i = 6; i > -1; --i)
						{
							ret = (ret << 8) | buf[i];
						}
						lua_pushinteger(L, ret);	
					}
				}
				else 
				{
					lua_pushinteger(L, *(int64_t*)buf);
				}
				return 1;
			}
	}
	else 
	{
		luaL_error(L, "readsockint, invalid len: %d", lenbytes);
	}
	return 0;
}
static int read_socksep(lua_State* L, struct lua_sock_session* sock, int from, int isLine)
{
	if (sock->unread < 1)  // no data
		{
			return 0;
		}
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	char chPre, chTmp;
	char sep = '\n';    // default sep
	if(!isLine)  // not readline
	{
		const char* strSep = lua_tostring(L, from);
		if (strSep)
		{
			sep = strSep[0];
		}
	}
	// find sep
	int i, j, bingo = -1;
	char* tmpBuf;
	int szToSep = 0;
	struct net_readblock_head* block = sock->buf_head;
	while (block != NULL)
	{
		if (block->meta[IDX_SEP_FLAG] == sep)  // sep not exist in this block
		{
			szToSep += block->size - block->ridx;  
			block = block->next;
			continue;
		}
		tmpBuf = (void*)block + NET_MSG_READ_HEAD_LEN;
		j = block->size;
		for(i = block->ridx ; i < j ; ++i)  // it cur block
		{
			chTmp = tmpBuf[i]; 
			if (chTmp == sep)  // find sep
				{
					bingo = i;
					break;
				}
			if (isLine)  // readline, mark pre-char
			{
				chPre = chTmp;
			}
		}
		if (bingo > -1)  // found sep in cur block
			{
				szToSep += bingo - block->ridx;
				break;
			}
		else   // not found sep in cur block
			{
				block->meta[IDX_SEP_FLAG] = sep;
				szToSep += j - block->ridx;  
			}
		// check next block
		block = block->next;
	}
	if (bingo < 0)  // sep not found
		{
			return 0;
		}
	// found sep
	int sepNum = 1;
	if (isLine && chPre == '\r') // sep by \r\n
	{
		sepNum = 2;
		--szToSep;
	}
	if(szToSep == 0)  // no data, return empty str
	{
		while ( (block = sock->buf_head) != NULL )
		{
			int blkLeft = block->size - block->ridx;
			int read = sepNum > blkLeft ? blkLeft : sepNum;
			if ( (block->ridx+=read) >= block->size)
			{
				pop_sock_buf(ctx, sock);
				luapps_free(block, 1);
			} 
			sock->unread -= read;
			if ( (sepNum-=read) < 1)
			{
				break;
			}
		}
		lua_pushstring(L, "");
		lua_pushinteger(L, 0);
		return 2;
	}
	luaL_Buffer buf;
	luaL_buffinitsize(L, &buf, szToSep);
	int readLeft = szToSep + sepNum;
	int dataLeft = szToSep;
	sock->unread -= readLeft;
	while ((block = sock->buf_head) != NULL && readLeft > 0)
	{
		i = block->size - block->ridx;   // block left
		if(dataLeft > 0)  // read data
		{
			j = dataLeft > i ? i : dataLeft;
			dataLeft -= j;
			luaL_addlstring(&buf, (void*)block + NET_MSG_READ_HEAD_LEN + block->ridx, j);
		}
		else  // read sep
		{
			j = sepNum > i ? i : sepNum;
			sepNum -= j;
		}
		readLeft -= j;
		if ((block->ridx += j) >= block->size)  // block read done, pop & free
		{
			pop_sock_buf(ctx, sock);
			luapps_free(block, 1);
		} 
	}
	luaL_pushresult(&buf);
	lua_pushinteger(L, szToSep);
	return 2;
}
static int read_sockall(lua_State* L, struct lua_sock_session* sock, int from)
{
	int close = lua_toboolean(L, from);
	if (close && sock->unread > 0) // closed && has cached data
		{
			return _readsockstr(L, sock, sock->unread);
		}
	// not closed || no cached data
	return 0;
}
static int l_readsock(lua_State* L)
{
	// sock, type, arg
	struct lua_sock_session* sock = lua_touserdata(L, 1);
	if (sock == NULL)
	{
		luaL_error(L, "no sock session");
	}
	int type = luaL_checkinteger(L, 2);
	if (type == 1) // read string
		{
			return read_sockstr(L, sock, 3);
		}
	else if (type == 2) // read int
		{
			struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
			return read_sockint(L, sock, 3, ctx->pipes_ctx->thread->global);
		}
	else if (type == 5)  // read line
	{
		return read_socksep(L, sock, 3, 1);
	}
	else if (type == 3) // read by sep
		{
			return read_socksep(L, sock, 3, 0);
		}
	else if (type == 4)  // read all until close
		{
			return read_sockall(L, sock, 3);
		}
	else
	{
		luaL_error(L, "readsock, unknown type: %d", type);
	}
	return 0;
}
static int l_mem(lua_State* L)
{
	struct lua_sock_session* sock = lua_touserdata(L, 1);
	if (sock == NULL)
	{
		luaL_error(L, "no sock");
	}
	lua_pushinteger(L, sock->mem);
	return 1;
}
static int l_sockop(lua_State* L)
{
	struct lua_service_context* ctx = lua_touserdata(L, lua_upvalueindex(1));
	//
	int cmd = luaL_checkinteger(L, 1);
	switch (cmd)
	{
	case LPPS_NET_CMD_SEND: {
			return tcp_send(L, ctx, 2);
		}
	case LPPS_NET_CMD_SESSION_START: {
			return tcp_session_start(L, ctx, 2);
		}
	case LPPS_NET_CMD_CLOSE: {
			return tcp_session_close(L, ctx, 2);
		}
	case LPPS_NET_CMD_OPEN: {
			return tcp_connect(L, ctx, 2);
		}
	case LPPS_NET_CMD_LISTEN:
		{
			return do_tcp_listen(L, ctx, 2);
		}
	default: {
			return luaL_error(L, "unknown sockop cmd: %d", cmd);
		}
	}
	return 0;
}
static int l_bufconcat(lua_State* L)
{
	int cmd = luaL_checkinteger(L, 1);
	if (cmd == 1)  // init:   cmd, szRequire
		{
			size_t sz = luaL_checkinteger(L, 2);
			//void* buf = luapps_malloc(sz);
			lua_newuserdata(L, sz);
			return 1;
		}
	else if (cmd == 2)  // copy data: cmd, buf, bufUsed, bufCap, src, srcIdx, srcTotal
		{
			void* buf = lua_touserdata(L, 2);
			int bufUsed = luaL_checkinteger(L, 3);
			int bufCap = luaL_checkinteger(L, 4);
			void* src = lua_touserdata(L, 5) + NET_MSG_HEAD_LEN;
			int srcIdx = luaL_checkinteger(L, 6);
			int srcTotal = luaL_checkinteger(L, 7);
			int srcLeft = srcTotal - srcIdx;
			//
			int bufLeft = bufCap - bufUsed;
			if (bufLeft <= 0)   // exception:  copy done before
				{
					return luaL_error(L, "bufconcat, copy already done, cap=%d, used=%d", bufCap, bufUsed);
				}
			// return: done, copied, srcLeft
			int off = bufLeft - srcLeft;
			if (off <= 0)  // all copy done
				{
					memcpy(buf + bufUsed, src + srcIdx, bufLeft);
					lua_pushboolean(L, 1);
					lua_pushinteger(L, bufLeft);
					lua_pushinteger(L, -off);
					return 3;
				}
			else  // not yet, copy all
				{
					memcpy(buf + bufUsed, src + srcIdx, srcLeft);
					lua_pushboolean(L, 0);
					lua_pushinteger(L, srcLeft);
					lua_pushinteger(L, 0);
					return 3;
				}
		}
	return luaL_error(L, "bufconcat, unknown cmd: %d", cmd);
}
static int l_netmsgheadlen(lua_State* L)
{
	lua_pushinteger(L, NET_MSG_HEAD_LEN);
	return 1;
}

int luapps_socket_openlib(lua_State* L)
{
	luaL_checkversion(L);
	
	luaL_Reg l[] = {
		{ "newsock", l_newsock},
		{ "freesock", l_freesock},
		{ "pushsockbuf", l_pushsockbuf},
		{ "readsock", l_readsock},
		{ "mem", l_mem},
		//
		{ "sockop", l_sockop},
		{ "netunpack", l_netunpack},
		{ "nethead", l_nethead},
		{ "netmsgheadlen", l_netmsgheadlen},
		{ "bufconcat", l_bufconcat},
		
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