
#ifndef PIPES_SOCKET_H
#define PIPES_SOCKET_H

#include "pipes_tcp.h"
#include <stddef.h>
#include <stdint.h>

#define READ_PACK_MAX_SIZE 0xFFFF


#define NET_CMD_TCP_LISTEN 1
#define NET_CMD_TCP_ACCEPT 2
#define NET_CMD_TCP_RECV 3
#define NET_CMD_TCP_START 4
#define NET_CMD_TCP_CLOSE 5
#define NET_CMD_TCP_CONNECT 6
#define NET_CMD_TCP_SEND 7
#define NET_CMD_ENABLE_WRITE 8

#define NET_CMD_TIMEOUT 20
#define NET_CMD_GETADDR_RET 21

#define NET_CMD_SHUTDOWN 64

struct pipes_net_context;
struct pipes_net_thread_context;

//
struct pipes_net_context* pipes_net_create();
void pipes_net_destroy(struct pipes_net_context* net);

int pipes_net_tick(struct pipes_net_thread_context* netTh);

void pipes_net_thread_init(struct pipes_net_context* net);

int pipes_net_send_msg(struct pipes_net_context* net, void* data, size_t sz);

int pipes_net_start_session(struct pipes_net_context* net, uint32_t id, uint64_t source);

int pipes_net_close_tcp(struct pipes_net_context* net, uint32_t id);

int pipes_net_tcp_send(struct pipes_net_context* net, uint32_t id, void* buf, int off, int sz, int isTmpBuf);

int pipes_net_tcp_listen(struct pipes_net_context* net, int session, uint64_t worker, struct pipes_tcp_server_cfg* cfg);

int pipes_net_tcp_connect(struct pipes_net_context* net, int session, uint64_t worker, struct pipes_tcp_client_cfg* cfg);

int pipes_net_connect_timeout(struct pipes_net_context* net, int id);

int pipes_net_shutdown(struct pipes_net_context* net);

//

struct net_readblock_head
{
	uint16_t ridx;
	uint16_t size;
	char meta[4];
	struct net_readblock_head* next;
};

#define NET_MSG_HEAD_LEN 5
#define NET_MSG_READ_HEAD_LEN sizeof(struct net_readblock_head)

void* alloc_net_read_buf(int cmd, uint32_t id, unsigned char readFlag, size_t bufCap, size_t* szActual);
void* alloc_net_msg(int cmd, uint32_t id, size_t cap, size_t* szActual);
void* wrap_net_msg(int cmd, uint32_t id, void* ori, size_t szOri, size_t* szOut);
void* unwrap_net_msg(void* msg, size_t szMsg, int* cmd, uint32_t* id, size_t* szPayload);
int gain_net_msg_head(void* msg, uint32_t* id);
int gain_net_read_packsize(void* msg, int lenBytes);


//
struct net_tcp_listen_ret
{
	int succ;
	int err;
}
;
struct net_tcp_accept
{
	uint32_t conn_id;
	int port;
	char host[16];
};
struct net_tcp_connect_ret
{
	int ret;
};


#endif // !PIPES_SOCKET_H




