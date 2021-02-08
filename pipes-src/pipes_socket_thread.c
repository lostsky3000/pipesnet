
#include "pipes_socket_thread.h"

#include "pipes_api.h"

#include "pipes_adapter.h"
#include "spinlock.h"
#include "atomic.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SS_TYPE_TCP 1
#define SS_TYPE_UDP 2

#define READ_BUF_LEN 1024

struct msg_block
{
	void* msg;
	int cap;
	int used;
	struct msg_block* next;
};
struct sock_session_context
{
	struct spinlock lock;
	int refcnt;
	int fd;
	int type;
	uint32_t id;
	struct pipes_net_thread_context* net_thread;
	uint64_t source;
};
struct tcp_session_context
{
	struct sock_session_context session;
	struct pipes_tcp_context* tcp;
	int is_svr;
	int closed;
	//
	int cache_num;
	int cache_size;
	struct msg_block* cache_head;
	struct msg_block* cache_tail;
	//
	char tmp_buf[READ_BUF_LEN];
};

static void fill_sock_session(struct sock_session_context* ss, int type, uint64_t source, struct pipes_net_thread_context* netThread)
{
	ss->type = type;
	ss->source = source;
	ss->net_thread = netThread;
}
static struct tcp_session_context* new_tcp_session(uint64_t source, int isSvr, struct pipes_net_thread_context* netThread)
{
	size_t sz = sizeof(struct tcp_session_context);
	struct tcp_session_context* ctx = pipes_malloc(sz);
	memset(ctx, 0, sz);
	ATOM_INC(&ctx->session.refcnt);  // init ref-cnt
	SPIN_INIT((struct sock_session_context*)ctx);  
	fill_sock_session(&ctx->session, SS_TYPE_TCP, source, netThread);
	//
	ctx->is_svr = isSvr;
	return ctx;
}

void free_sock_session(struct sock_session_context* ss)
{
	int ref = ATOM_DEC(&ss->refcnt);
	if (ref == 0)
	{
		pipes_free(ss);
	}
	else if (ref < 0)   // error
	{
		printf("free_sock_session, invalid ref: %d\n", ref);
	}
}
void retain_sock_session(struct sock_session_context* ss)
{
	ATOM_INC(&ss->refcnt);
}

static int send_msg_to_worker(struct pipes_net_thread_context* netThread, uint64_t to, 
	int session, int cmd, uint32_t id, void* msg, size_t szMsg)
{
	size_t sz = 0;
	void* data = wrap_net_msg(cmd, id, msg, szMsg, &sz);
	pipes_api_send(netThread->sys_service, to, PMSG_TYPE_NET, session, data, sz);
	return 1;
}
static int send_tcp_server_ret(struct pipes_net_thread_context* ctx, int isSucc, uint32_t id,
	uint64_t to, int session)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	//
	struct net_tcp_listen_ret ret; 
	ret.succ = isSucc;
	//
	send_msg_to_worker(ctx, to, -session, NET_CMD_TCP_LISTEN, id, &ret, sizeof(ret));
	return 1;
}
static int flush_cached_msg(struct tcp_session_context* ctx, int destroy);
int proc_net_thread_msg(struct pipes_net_thread_context* ctx, struct pipes_message* msg)
{
	int cmd = 0;
	uint32_t id = 0;
	size_t szPayload = 0;
	void* data = unwrap_net_msg(msg->data, pipes_api_msg_sizeraw(msg->size), &cmd, &id, &szPayload);
	switch (cmd)
	{
	case NET_CMD_TCP_SESSION_START: {
		void* tmp = pipes_handle_grab(id, ctx->handle_mgr);
		if (tmp == NULL) // session not found;
		{	// resp worker conn closed
			send_msg_to_worker(ctx, msg->source, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
			break;
		}
		struct sock_session_context* ss = (struct sock_session_context*)tmp;
		if (ss->source != 0 || ss->type != SS_TYPE_TCP) // source has been set | not tcp 
		{
			break;
		}
		struct tcp_session_context* tcp = (struct tcp_session_context*)tmp;
		if (tcp->is_svr)  // is svr, no need call session-start
		{
			break;
		}
		if (tcp->closed)  // tcp closed
		{	// resp worker conn closed
			send_msg_to_worker(ctx, msg->source, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
			break;
		}
		struct net_tcp_session_start* req = (struct net_tcp_session_start*)data;
		ss->source = req->source;  // assign source
		flush_cached_msg(tcp, 0);  // flush cached msg
		break;
	}
	case NET_CMD_TCP_CLOSE: {
		void* tmp = pipes_handle_grab(id, ctx->handle_mgr);
		if (tmp == NULL) // session not found;
		{
			break;
		}
		struct sock_session_context* ss = (struct sock_session_context*)tmp;
		if (ss->type != SS_TYPE_TCP)	// not tcp 
		{
			break;
		}
		struct tcp_session_context* tcp = (struct tcp_session_context*)tmp;
		if (tcp->closed)  // session has closed
		{
			break;
		}
		tcp->closed = 1;
		flush_cached_msg(tcp, 1); // clear cached msg
		uint64_t source = ss->source;
		pipes_net_tcp_close(ctx->net_ctx, tcp->tcp);
		pipes_handle_retire(id, ctx->handle_mgr);
		if (tcp->is_svr)  // listen socket
		{}
		else
		{}
		if (source > 0)   // notify
		{
			send_msg_to_worker(ctx, source, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
		}
		free_sock_session(ss);
		break;
	}
	case NET_CMD_TCP_LISTEN: {
		// create tcp server 
		struct pipes_tcp_server_cfg* cfg = (struct pipes_tcp_server_cfg*)data;
		struct tcp_session_context* ssTcp = new_tcp_session(msg->source, 1, ctx);
		struct pipes_tcp_context* tcp = pipes_net_tcp_server(ctx->net_ctx, cfg, ssTcp);
		int succ = tcp != NULL;
		uint32_t handle = 0;
		if(succ) // listen succ, add to handle-mgr
		{
			handle = (uint32_t)pipes_handle_register((struct sock_session_context*)ssTcp, 0, ctx->handle_mgr);
			ssTcp->tcp = tcp;
		}else // listen failed, free session-context
		{
			free_sock_session((struct sock_session_context*)ssTcp);
		}
		send_tcp_server_ret(ctx, succ, handle, msg->source, msg->session);
		break;
	}
	default:
		{
			printf("unknown net msg cmd: %ld\n", msg->dest);
			break;
		}
	}
	return 1;
}

void* net_on_accept(struct pipes_tcp_context* tcp, const char* host, int port, void* ud)
{
	//printf("new conn in, %s:%d\n", host, port);
	struct tcp_session_context* ssListen = (struct tcp_session_context*)ud;
	struct pipes_net_thread_context* netTh = ssListen->session.net_thread;
	struct tcp_session_context* ssConn = new_tcp_session(0, 0, netTh);
	ssConn->tcp = tcp;
	// add to session-mgr
	uint32_t id = (uint32_t)pipes_handle_register((struct sock_session_context*)ssConn, 0, netTh->handle_mgr);
	// notify
	struct net_tcp_accept ret;
	ret.conn_id = id;
	ret.port = port;
	strcpy(ret.host, host);
	send_msg_to_worker(netTh, ssListen->session.source, 0, NET_CMD_TCP_ACCEPT, ssListen->session.id, &ret, sizeof(ret));
	return ssConn;
}

#define CACHE_BLOCK_SIZE 128
static void push_cache_msg(struct tcp_session_context* ctx, struct msg_block* msg)
{
	if (ctx->cache_tail == NULL)  // no msg cached
		{
			ctx->cache_head = msg;
		}
	else
	{
		ctx->cache_tail->next = msg;
	}
	ctx->cache_tail = msg;
	++ctx->cache_num;
	ctx->cache_size += msg->used;
}
static void new_cache_msg(struct tcp_session_context* ctx, int bufCap)
{
	size_t sz = sizeof(struct msg_block);
	struct msg_block* msg = pipes_malloc(sz);
	memset(msg, 0, sz);
	//
	msg->msg = alloc_net_msg(NET_CMD_TCP_RECV, ctx->session.id, bufCap, &sz);
	msg->cap = sz;
	msg->used = sz - bufCap;
	//
	push_cache_msg(ctx, msg);
}
static int cache_msg(struct tcp_session_context* ctx, char* buf, int size)
{
	struct msg_block* tail = ctx->cache_tail;
	if (tail == NULL) // no msg cached, create msg block
	{
		new_cache_msg(ctx, CACHE_BLOCK_SIZE);
	}
	tail = ctx->cache_tail;
	if (tail->cap - tail->used >= size)  // cur tail enough
		{
			memcpy(tail->msg + tail->used, buf, size);
			tail->used += size;
			ctx->cache_size += size;
		}
	else // not enough, new tail
	{
		size_t cap = CACHE_BLOCK_SIZE < size + NET_MSG_HEAD_LEN ? size + NET_MSG_HEAD_LEN : CACHE_BLOCK_SIZE;
		new_cache_msg(ctx, cap);
		tail = ctx->cache_tail;
		memcpy(tail->msg + tail->used, buf, size);
		tail->used += size;
		ctx->cache_size += size;
	}
	return 1;
}
static int flush_cached_msg(struct tcp_session_context* ctx, int destroy)
{
	struct sock_session_context* ss = (struct sock_session_context*)ctx;
	struct pipes_net_thread_context* netTh = ss->net_thread;
	uint64_t to = ss->source;
	//
	struct msg_block* head = NULL;
	while ( (head=ctx->cache_head) != NULL )
	{
		ctx->cache_head = head->next;
		head->next = NULL;
		if (destroy)
		{
			ADAPTER_FREE(head->msg);
		}
		else
		{	// send
			
			pipes_api_send(netTh->sys_service, to, PMSG_TYPE_NET, 0, head->msg, head->used);
		}
		// free msgblock
		pipes_free(head);
	}
	ctx->cache_tail = NULL;
	ctx->cache_num = 0;
	ctx->cache_size = 0;
	return 1;
}
char* net_on_prepare_readbuf(int* szRead, void*ud)
{
	struct tcp_session_context* ss = (struct tcp_session_context*)ud;
	*szRead = READ_BUF_LEN;
	return ss->tmp_buf;
}
int net_on_tcp_recv(char* buf, int read, void* ud)
{
	//printf("tcp recv, read=%d\n", read);
	struct tcp_session_context* ss = (struct tcp_session_context*)ud;
	uint64_t to = ss->session.source;
	if (to == 0)  // session not start, cache msg
	{
		cache_msg(ss, buf, read);
	}
	else   // send to worker
	{
		struct pipes_net_thread_context* netTh = ss->session.net_thread;
		send_msg_to_worker(netTh, to, 0, NET_CMD_TCP_RECV, ss->session.id, buf, read);
	}
	return 1;
}
void net_on_tcp_close(void* ud)
{
	//printf("disconn\n");
	struct tcp_session_context* ss = (struct tcp_session_context*)ud;
	ss->closed = 1;
	flush_cached_msg(ss, 1);   //clear all cached msg
	// unreg id
	struct pipes_net_thread_context* netTh = ss->session.net_thread;
	uint32_t id = ss->session.id;
	uint64_t to = ss->session.source;
	pipes_handle_retire(id, netTh->handle_mgr);
	if (to > 0)   // session has started, notify worker
	{
		send_msg_to_worker(netTh, to, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
	}
	// free sock session
	free_sock_session((struct sock_session_context*)ss);
}

//
uint64_t net_get_session_handle(void* ctx)
{
	struct sock_session_context* ss = (struct sock_session_context*)ctx;
	return ss->id;
}
void net_set_session_handle(void* ctx, uint64_t handle)
{
	struct sock_session_context* ss = (struct sock_session_context*)ctx;
	ss->id = (uint32_t)handle;
}

