
#include "pipes_socket.h"
#include "pipes_macro.h"
#include "pipes_malloc.h"
#include "pipes_tcp.h"
#include "pipes_adapter.h"
#include "spinlock.h"
#include "pipes.h"
#include "atomic.h"
#include "pipes_server.h"
#include <string.h>
#include <stdio.h>



#define MALLOC pipes_malloc
#define FREE pipes_free

#define MAX_SOCK_SESSION 1<<16

#define HASH_ID(hid) hid%(MAX_SOCK_SESSION)

#define SOCK_TYPE_INVALID 0
#define SOCK_TYPE_LOCAL 1
#define SOCK_TYPE_LISTEN 2
#define SOCK_TYPE_TCP_CONN 3
#define SOCK_TYPE_TCP 4
#define SOCK_TYPE_UDP 5

//
#ifdef SYS_IS_LINUX

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <errno.h>

#define MAX_EPOLL_EVENT 1024
#define EPOLL_WAIT_MS 5000

#define READ_BLOCK_MAX_SIZE 4096    // 2bytes

#define TMP_BUF_SIZE 4096


//
struct req_head
{
	int cmd;
};
struct req_shutdown
{
	struct req_head head;
};
struct req_tcp_listen
{
	struct req_head head;
	int session;
	uint64_t worker;
	struct pipes_tcp_server_cfg cfg;
};
struct req_tcp_connect
{
	struct req_head head;
	int session;
	uint64_t worker;
	struct pipes_tcp_client_cfg cfg;
};
struct req_connect_timeout
{
	struct req_head head;
	int id;
};
struct req_getaddr_ret
{	//cmd, id, fd, err
	struct req_head head;
	int id;
	int fd;
	int err;
};
struct req_start_session
{
	struct req_head head;
	int id;
	uint64_t worker;
};
struct req_send
{
	struct req_head head;
	int id;
	int off;
	int size;
	void* buf;
};
struct req_close
{
	struct req_head head;
	int id;
};
struct req_enable_write
{
	struct req_head head;
	int id;
};

//
struct send_buffer
{
	int off;
	int size;
	void* buf;
	struct send_buffer* next;
};
struct send_list
{
	struct send_buffer* head;
	struct send_buffer* tail;
};
struct direct_send_list
{
	struct send_buffer* head;
	struct send_buffer* tail;
};

struct tcp_reader
{
	int on_read_len;
	int pack_size;
	int pack_read;
	int block_size;
	int block_read;
	void* buf_block;
};
struct socket
{
	struct spinlock lock;
	int id;
	int fd;
	int type;  
	int event;
	int closed;				// ATOM
	int call_close;   
	int sending_ref;
	//
	int session;
	//
	struct tcp_decoder* decoder;
	struct tcp_reader* reader;
	//
	uint64_t worker;
	int64_t send_bytes;
	int64_t recv_bytes;
	//
	struct send_list send_list;
	struct direct_send_list direct_send_list;
	char runbuf[sizeof(struct tcp_decoder) + sizeof(struct tcp_reader)];
};
struct socket_closing
{
	int id;
	int cd;
};
struct pipes_net_context
{
	int closed;
	int fd;
	int sockpair_read;
	int sockpair_write;
	//
	int id_cnt;
	int closing_num;
	//
	fd_set cmd_rfds;
	//
	char buf_cli_addr[INET_ADDRSTRLEN];
	struct epoll_event events[MAX_EPOLL_EVENT];
	char tmp_buf[TMP_BUF_SIZE];
	struct socket_closing slot_closing[MAX_SOCK_SESSION];
	struct socket slot[MAX_SOCK_SESSION];
};
//
static void socket_lock(struct socket* s)
{
	spinlock_lock(&s->lock);
}
static int socket_trylock(struct socket* s)
{
	return spinlock_trylock(&s->lock);
}
static void socket_unlock(struct socket* s)
{
	spinlock_unlock(&s->lock);
}

//
static struct socket* get_sock_session(struct pipes_net_context* net, int id)
{
	return &net->slot[HASH_ID(id)];
}
static int set_non_blocking(int fd)
{
	int oldOpt = fcntl(fd, F_GETFL);
	int newOpt = oldOpt | O_NONBLOCK;
	fcntl(fd, F_SETFL, newOpt);
	return oldOpt;
}
static void
set_keepalive(int fd) {
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));  
}
static void set_tcp_nodelay(int fd)
{
	int on = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}
static int add_fd(int epoll, int fd, uint32_t events, void* ud)
{	
	struct epoll_event event;
	event.data.ptr = ud;
	event.events = events;
	int ret = epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event);
	return ret;
}
static int mod_fd(int epoll, int fd, uint32_t events, void* ud)
{
	struct epoll_event event;
	event.data.ptr = ud;
	event.events = events;
	int ret = epoll_ctl(epoll, EPOLL_CTL_MOD, fd, &event);
	return ret;
}
static int del_fd(int epoll, int fd)
{
	/*
	BUGS
       In  kernel versions before 2.6.9, the EPOLL_CTL_DEL operation required a non-NULL pointer in event, even though this argu©\
       ment is ignored.  Since Linux 2.6.9, event can be specified as NULL when using EPOLL_CTL_DEL.  Applications that  need  to
       be portable to kernels before 2.6.9 should specify a non-NULL pointer in event. 
	*/
	struct epoll_event evt;
	epoll_ctl(epoll, EPOLL_CTL_DEL, fd, &evt);
	return 1;
}
static void close_fd(int epoll, int fd)
{
	if (fd > 0)
	{
		del_fd(epoll, fd);
		close(fd);
	}
}
static void set_sock_decoder(struct socket* s, struct tcp_decoder* ori)
{
	s->decoder = (struct tcp_decoder*)s->runbuf;
	*s->decoder = *ori;
	//if (ori->type == TCP_DECODE_FIELD_LENGTH)
	{
		size_t sz = sizeof(struct tcp_decoder);
		struct tcp_reader* rd;
		rd = (struct tcp_reader*)(s->runbuf + sz);
		s->reader = rd;
		sz = sizeof(struct tcp_reader);
		memset(rd, 0, sz);
		rd->on_read_len = 1;
	}
}
//
struct pipes_net_context* pipes_net_create()
{
	int fd = epoll_create(MAX_EPOLL_EVENT);
	size_t sz = sizeof(struct pipes_net_context);
	struct pipes_net_context* ctx = pipes_malloc(sz);
	memset(ctx, 0, sz);
	ctx->fd = fd;
	ctx->closed = 0;
	ctx->id_cnt = 0;
	ctx->closing_num = 0;
	int i;
	for (i = 0; i < MAX_SOCK_SESSION; ++i)
	{
		struct socket* s = &ctx->slot[i];
		SPIN_INIT(s);
		s->type = SOCK_TYPE_INVALID;
		s->closed = 1;
		//
		struct socket_closing* sc = &ctx->slot_closing[i];
		sc->id = 0;
	}
	// init
	int arr[2] = { 0, 0 };
	int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, arr);
	if (ret == -1)
	{
		printf(">>>>>> init socketpair error: %d\n", errno);
		return NULL;
	}
	ctx->sockpair_write = arr[0];
	ctx->sockpair_read = arr[1];
	//
	FD_ZERO(&ctx->cmd_rfds);
	return ctx;
};
static void force_close(struct pipes_net_context* net, struct socket* s);
void pipes_net_destroy(struct pipes_net_context* net)
{
	int i;
	for (i = 0; i < MAX_SOCK_SESSION; ++i)
	{
		struct socket* s = &net->slot[i];
		if (s->fd > 0)
		{
			close_fd(net->fd, s->fd);
			s->fd = -1;
		}
		force_close(net, s);
	}
	close_fd(net->fd, net->sockpair_read);
	pipes_free(net);
}
static void init_send_list(struct socket* s)
{
	struct send_list* ls = &s->send_list;
	ls->head = NULL;
	ls->tail = NULL;
}
static void init_direct_send_list(struct socket* s)
{
	struct direct_send_list* ls = &s->direct_send_list;
	ls->head = NULL;
	ls->tail = NULL;
}
static int new_id(struct pipes_net_context* net, int fd, int type, uint64_t worker)
{
	int i;
	for (i = 0; i < MAX_SOCK_SESSION; ++i)
	{
		int id = ++net->id_cnt;
		if (id < 0)
		{
			id = 1;
			net->id_cnt = 1;
		}
		struct socket* s = get_sock_session(net, id);
		if(s->type == SOCK_TYPE_INVALID)  // free slot
		{
			s->fd = fd;
			s->type = type;
			s->id = id;
			s->worker = worker;
			s->send_bytes = 0;
			s->recv_bytes = 0;
			s->event = 0;
			s->decoder = NULL;
			s->reader = NULL;
			s->sending_ref = 0;
			init_send_list(s);
			init_direct_send_list(s);
			s->call_close = 0;
			ATOM_CAS(&s->closed, 1, 0);
			return id;
		}
	}
	return -1;
}
void pipes_net_thread_init(struct pipes_net_context* net)
{
	int id = new_id(net, net->sockpair_read, SOCK_TYPE_LOCAL, 0);
	struct socket* s = get_sock_session(net, id);
	s->event = EPOLLIN;
	add_fd(net->fd, s->fd, s->event, s);
}

//
static int send_rawmsg_to_worker(struct pipes_net_thread_context* net,
	uint64_t to,
	int session,
	void* msg,
	size_t szMsg)
{
	pipes_api_send(net->sys_service, to, PMSG_TYPE_NET, -session, msg, szMsg);
	return 1;
}
static int send_msg_to_worker(struct pipes_net_thread_context* net,
	uint64_t to, 
	int session,
	int cmd,
	uint32_t id,
	void* msg,
	size_t szMsg)
{
	size_t sz = 0;
	void* data = wrap_net_msg(cmd, id, msg, szMsg, &sz);
	pipes_api_send(net->sys_service, to, PMSG_TYPE_NET, -session, data, sz);
	return 1;
}

//
static char* alloc_readbuf(struct pipes_net_context* net, struct socket* sock, int* szBuf)
{
	struct tcp_decoder* dec = sock->decoder;
	if (dec->type == TCP_DECODE_FIELD_LENGTH)
	{
		struct tcp_reader* rd = sock->reader;
		if (rd->on_read_len)   // on reading length
		{ 
			*szBuf = dec->val1 - rd->pack_read;
			return net->tmp_buf + rd->pack_read;
		}
		else // on reading payload
		{
			if (rd->buf_block == NULL)  // new block
			{
				int sz = rd->pack_size - rd->pack_read;  // left size of cur pack
				char part1st;
				if ((part1st = (rd->pack_read == 0)))  // 1st part, add pack-size
				{
					sz += dec->val1;     //add lenbytes
				}
				if (sz > READ_BLOCK_MAX_SIZE)   // over max blocksize
				{
					sz = READ_BLOCK_MAX_SIZE;
				}
				size_t szActual;
				void* buf = alloc_net_read_buf(NET_CMD_TCP_RECV, sock->id, 0, sz, &szActual);
				rd->block_size = sz;
				rd->buf_block = buf;
				if (part1st)   // 1st part, write pack-size
				{
					if (dec->val1 == 2) //2bytes
					{
						*(uint16_t*)(rd->buf_block + NET_MSG_READ_HEAD_LEN) = htons(rd->pack_size);
					}
					else //4bytes
					{
						*(uint32_t*)(rd->buf_block + NET_MSG_READ_HEAD_LEN) = htonl(rd->pack_size);
					}
					rd->block_read = dec->val1;
				}
				else
				{
					rd->block_read = 0;
				}
			}
			*szBuf = rd->block_size - rd->block_read;
			return rd->buf_block + NET_MSG_READ_HEAD_LEN + rd->block_read;
		}
	}
	*szBuf = TMP_BUF_SIZE;
	return net->tmp_buf;
}
static void on_readbuf_unused(struct socket* sock, char* buf, size_t sz)
{
	struct tcp_decoder* dec = sock->decoder;
	if (dec->type == TCP_DECODE_FIELD_LENGTH)
	{
		struct tcp_reader* rd = sock->reader;
		if (rd->on_read_len)  // on reading length
		{	   
		}
		else  // on read payload, free
		{
			if (buf - NET_MSG_READ_HEAD_LEN - rd->block_read == rd->buf_block)
			{
				ADAPTER_FREE(rd->buf_block);
				rd->buf_block = NULL;
			}
			else  // exception
			{
				printf("===============!!!!!!!!!! on_readbuf_unused, buf error, headlen=%ld, read=%d\n",
					NET_MSG_READ_HEAD_LEN,
					rd->block_read);
			}
		}
	}
}

static int on_tcp_recv(
	struct pipes_net_thread_context* netTh, 
	struct pipes_net_context* net,
	struct socket* sock,
	char* buf,
	int szRead)
{
	sock->recv_bytes += szRead;
	int ret = 0;
	//printf("on_tcp_recv, fd=%d, id=%d, len=%d\n", sock->fd, sock->id, szRead);
	if (sock->worker == 0)  // strict check
		{
			printf(">>>>>> on_tcp_recv error, worker not set, id=%d, fd=%d\n", sock->id, sock->fd);
			return 0;
		}
	struct tcp_decoder* dec = sock->decoder;
	struct tcp_reader* rd = sock->reader;
	if (dec->type == TCP_DECODE_FIELD_LENGTH)
	{	
		if (rd->on_read_len)  // on reading length
			{
				rd->pack_read += szRead;
				if (rd->pack_read == dec->val1)  // read length done
					{
						rd->on_read_len = 0;     // change read state
						rd->pack_read = 0;     // reset length has read
						if(dec->val1 == 2) // 2bytes for length
						{	
							rd->pack_size = ntohs(*(uint16_t*)(net->tmp_buf));
						}
						else  // 4bytes for length
						{
							rd->pack_size = ntohl(*(uint32_t*)(net->tmp_buf));
						}
						if (rd->pack_size > dec->maxlength)   // over limit
							{
								rd->pack_size = dec->maxlength;
							}
						if (rd->pack_size < 1)   // invalid length
							{
								rd->pack_size = 128;     // make safe
								    // close?
							}
						rd->pack_read = 0;
						//printf("read len done: %d\n", rd->pack_size); // debug
						ret = 1;  // read payload immediately
					}
				else  // read length not done, continue
					{}
			}
		else  // on reading payload
			{
				rd->block_read += szRead;
				rd->pack_read += szRead;
				if (rd->block_read == rd->block_size)  // block read done, notify
					{
						struct net_readblock_head* head = (struct net_readblock_head*)rd->buf_block;
						head->size = rd->block_size;
						send_rawmsg_to_worker(netTh, sock->worker, 0, rd->buf_block, rd->block_size + NET_MSG_READ_HEAD_LEN);
						rd->buf_block = NULL;
						rd->block_read = 0;
					}
				if (rd->pack_read == rd->pack_size)  // pack read done, change read state
					{
						rd->on_read_len = 1;
						rd->pack_read = 0;
					}
			}
	}
	else
	{   // notify worker
		size_t szActual;
		void* bufOut = alloc_net_read_buf(NET_CMD_TCP_RECV, sock->id, 0, szRead, &szActual);
		struct net_readblock_head* head = (struct net_readblock_head*)bufOut;
		head->size = szRead;
		memcpy(bufOut + NET_MSG_READ_HEAD_LEN, buf, szRead);
		send_rawmsg_to_worker(netTh, sock->worker, 0, bufOut, szActual);
	}
	return ret;
}
static void init_tcp_fd(int fd)
{
	// debug begin
	int optVal = 1024;
	socklen_t optLen = sizeof(optVal);
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &optVal, optLen);
	getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &optVal, &optLen);
	// debug end	
}
static void on_tcp_accept(struct pipes_net_thread_context* netTh, 
	struct socket* sockListen,
	int fd,
	struct net_tcp_accept* ret)
{
	set_keepalive(fd);
	set_non_blocking(fd);
	set_tcp_nodelay(fd);
	
	init_tcp_fd(fd);
	
	//
	struct pipes_net_context* net = netTh->net_ctx;
	int id = new_id(net, fd, SOCK_TYPE_TCP, 0);
	if (id < 1)  // reach max socket limit
	{	// do sth
		close(fd);
		printf(">>>>>> on_tcp_accept error, reach max socket limit, %d, fd=%d\n", MAX_SOCK_SESSION, fd);
		return ;
	}
	//printf("on_tcp_accept: %s:%d, id=%d, fd=%d\n", ret->host, ret->port, id, fd);  
	struct socket* sock = get_sock_session(net, id);
	set_sock_decoder(sock, sockListen->decoder);     // set decoder
	//
	add_fd(net->fd, fd, sock->event, sock);  // add dummy event
	// notify worker
	ret->conn_id = id;
	send_msg_to_worker(netTh, sockListen->worker, 0, NET_CMD_TCP_ACCEPT, sockListen->id, ret, sizeof(struct net_tcp_accept));
}
static struct send_buffer* pop_send_list(struct send_list *ls)
{
	struct send_buffer* head = ls->head;
	if (head)
	{
		ls->head = head->next;  // move to next
		if(ls->head == NULL)  // list empty
		{
			ls->tail = NULL;
		}
		head->next = NULL;
	}
	return head;
}
static struct send_buffer* pop_direct_send_list(struct direct_send_list *ls)
{
	struct send_buffer* head = ls->head;
	if (head)
	{
		ls->head = head->next;   // move to next
		if(ls->head == NULL)  // list empty
		{
			ls->tail = NULL;
		}
		head->next = NULL;
	}
	return head;
}
static void force_close(struct pipes_net_context* net, struct socket* s)
{
	if (s->fd > 0)
	{
		del_fd(net->fd, s->fd);
		//close_fd(net->fd, s->fd);
		s->fd = -1;
	}
	s->id = 0;
	s->type = SOCK_TYPE_INVALID;
	s->worker = 0;
	//
	if(s->reader && s->reader->buf_block)
	{
		ADAPTER_FREE(s->reader->buf_block);
	}
	//
	struct send_buffer* buf;
	struct direct_send_list* dsl = &s->direct_send_list;
	while ( (buf = pop_direct_send_list(dsl)) != NULL )
	{
		FREE(buf->buf);
		FREE(buf);
	}
	struct send_list* sl = &s->send_list;
	while ( (buf = pop_send_list(sl)) != NULL )
	{
		FREE(buf->buf);
		FREE(buf);
	}
	s->sending_ref = 0;
}
static void on_tcp_close(struct pipes_net_thread_context* netTh, struct pipes_net_context* net, struct socket* s)
{
	//printf("on_tcp_close, id=%d, fd=%d, recv=%ld, send=%ld\n", s->id, s->fd, s->recv_bytes, ATOM_ADD(&s->send_bytes, 0));
	del_fd(net->fd, s->fd);
	s->fd = -1;
	ATOM_CAS(&s->closed, 0, 1);
	if (!socket_trylock(s))
	{
		// add to closing task list
		struct socket_closing* sc = &net->slot_closing[net->closing_num++];
		sc->id = s->id;
		sc->cd = 10;
		return;
	}
	uint64_t to = s->worker;
	int id = s->id;
	force_close(net, s);
	socket_unlock(s);
	//
	if(to > 0)
	{
		send_msg_to_worker(netTh, to, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
	}
}
static int has_worker_msg(struct pipes_net_context* net);
static void on_recv_worker_msg(struct pipes_net_context* net, struct pipes_net_thread_context* netTh);
static void on_connect_ret(
	struct pipes_net_thread_context* netTh, 
	struct pipes_net_context* net,
	int session,
	struct socket* sock,
	uint64_t worker,
	int ret,
	int fromEpoll);
static void inc_sending_ref(struct socket* s)
{
	ATOM_INC(&s->sending_ref);
}
static void dec_sending_ref(struct socket* s)
{
	ATOM_DEC(&s->sending_ref);
}
static void sub_sending_ref(struct socket* s, int num)
{
	ATOM_SUB(&s->sending_ref, num);
}
static int send_list_empty(struct socket* s)
{
	struct send_list* ls = &s->send_list;
	return ls->head == NULL;
}
static int no_sending_data(struct socket* s)
{
	if (s->direct_send_list.head == NULL && send_list_empty(s) && ATOM_ADD(&s->sending_ref, 0) == 0)
	{
		return 1;
	}
	return 0;
}
static int64_t add_send_bytes(struct socket* s, int num)
{
	return ATOM_ADD(&s->send_bytes, num);
}
static void on_tcp_can_send(struct pipes_net_thread_context* netTh, struct pipes_net_context* net, struct socket* s)
{
	if (s->id == 4)   // debug
	{
		int n = 1;
	}
	struct send_list *sl = &s->send_list;
	if (!socket_trylock(s))
	{
		return;
	}
	// check direct send-list
	struct direct_send_list* dsl = &s->direct_send_list;
	if (dsl->tail != NULL)  // has data, link to send list
	{
		dsl->tail->next = sl->head;
		//
		sl->head = dsl->head;
		if(sl->tail == NULL)  // send list empty
		{
			sl->tail = dsl->tail;
		}
		// clear direct send list
		dsl->head = NULL;
		dsl->tail = NULL;
	}
	socket_unlock(s);
	// flush send list
	int totalSend = 0;
	int sndnum = 0;
	int succBufNum = 0;
	struct send_buffer* buf = NULL;
	while ((buf = sl->head) != NULL)
	{
		sndnum = send(s->fd, buf->buf + buf->off, buf->size, 0);
		if (sndnum == -1)  // send failed, try next tick
		{
			break;
		}
		if (sndnum >= buf->size)  // buf send done
		{
			++succBufNum;
			pop_send_list(sl);    // pop from send list
			FREE(buf->buf);
			FREE(buf);
		}
		else  // send partly, update off and size
		{
			buf->off += sndnum;
			buf->size -= sndnum;
		}
		totalSend += sndnum;
	}
	if (succBufNum > 0)  // update send list ref
	{
		sub_sending_ref(s, succBufNum);
	}
	if (totalSend > 0)
	{
		add_send_bytes(s, totalSend);
		//printf("send list, id=%d, size=%d, totalSend=%ld\n", s->id, totalSend, ATOM_ADD(&s->send_bytes, 0));
	}
	if (send_list_empty(s)) // send list empty, remove epollout
	{
		if (s->event & EPOLLOUT)
		{
			s->event = EPOLLIN;
			mod_fd(net->fd, s->fd, s->event, s);
		}
		if (s->call_close)   // close called, real close
		{
			close(s->fd);
			on_tcp_close(netTh, net, s);
		}
	}
}
static int flush_closing_task(struct pipes_net_thread_context* netTh, struct pipes_net_context* net)
{
	int closedNum = 0;
	if (net->closing_num < 1)
	{
		return closedNum;
	}
	int i;
	for (i=0; i<net->closing_num; ++i)
	{
		struct socket_closing* sc = &net->slot_closing[net->closing_num - 1];
		struct socket* s = get_sock_session(net, sc->id);
		if (--sc->cd >= 0)
		{	
			if (!socket_trylock(s))
			{
				break;  // try lock failed
			}
		}
		else
		{
			socket_lock(s);
		}
		++closedNum;
		uint64_t to = s->worker;
		int id = sc->id;
		force_close(net, s);
		socket_unlock(s);
		sc->id = 0;
		sc->cd = 0;
		--net->closing_num;
		//
		if(to > 0)  // notify worker
		{
			send_msg_to_worker(netTh, to, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
		}
	}
	return closedNum;
}
int pipes_net_tick(struct pipes_net_thread_context* netTh)
{
	struct pipes_net_context* ctx = netTh->net_ctx;
	struct epoll_event* evts = ctx->events;
	int i;
	int ret = 0;
	if (ctx->closing_num > 0)  // has closing task
	{
		i = flush_closing_task(netTh, ctx);
		ret = epoll_wait(ctx->fd, evts, MAX_EPOLL_EVENT, 0);
		if (ret < 1)  // no epoll event, return close num
		{
			return i;
		}
	}
	else
	{
		ret = epoll_wait(ctx->fd, evts, MAX_EPOLL_EVENT, EPOLL_WAIT_MS);	
	}
	if (ret < 0)
	{
		//printf("epoll_wait error: %d\n", errno);
		return ret;
	}
	if (ret == 0)
	{
		//printf("epoll_wait, no event\n");
		return 0;
	}
	struct epoll_event* evt;
	struct socket* sock;
	//
	struct sockaddr_in inAddrCli;
	socklen_t addrLenCli = sizeof(inAddrCli);
	int val = -1;
	int type;
	int fd;
	int szBuf = 0;
	uint32_t events;
	char* bufRead = NULL;
	//
	for(i = 0 ; i < ret ; ++i)
	{	
		evt = &evts[i];
		sock = (struct socket*)(evt->data.ptr);
		if (sock == NULL)
		{
			printf("epoll_wait exception: no socket, events=%d, fd=%d\n", evt->events, evt->data.fd);
			continue;
		}
		type = sock->type;
		fd = sock->fd;
		events = evt->events;
		if (type == SOCK_TYPE_TCP)
		{
			if (sock->id == 4)  // debug
			{
				int n = 1;
			}
			if (events & EPOLLIN)
			{	
				//printf("EPOLLIN \n");  // debug
				while (1)
				{
					bufRead = alloc_readbuf(ctx, sock, &szBuf);
					val = recv(fd, bufRead, szBuf, 0);
					if (val > 0)
					{
						if (!on_tcp_recv(netTh, ctx, sock, bufRead, val))
						{
							break;   // no need recv again immediately
						}
					}
					else
					{
						break;
					}
				}
				if (val == 0 || (val < 0 && errno != EAGAIN && errno != EWOULDBLOCK))  // conn down
					{
						on_readbuf_unused(sock, bufRead, szBuf);
						on_tcp_close(netTh, ctx, sock);
					}
			}
			//else if (events & EPOLLOUT)
			if(events & EPOLLOUT)
			{
				//printf("EPOLLOUT \n");
				on_tcp_can_send(netTh, ctx, sock);
			}
		}
		else if (type == SOCK_TYPE_LISTEN)  // 
			{
				//printf("listen in\n");
				int cnt = 0;
				while ((val = accept(fd, (struct sockaddr*)&inAddrCli, &addrLenCli)) >= 0)
				{
					struct net_tcp_accept retAccept;
					retAccept.port = ntohs(inAddrCli.sin_port); 
					inet_ntop(AF_INET, &inAddrCli.sin_addr, retAccept.host, INET_ADDRSTRLEN);
					//printf("new conn, fd=%d, remote=%s:%d\n", val, ctx->buf_cli_addr, portCli);
					// cb
					on_tcp_accept(netTh, sock, val, &retAccept);
					if(++cnt >= 50)
					{
						break;
					}
				}
			}
		else if (type == SOCK_TYPE_LOCAL)  // msg from worker
			{
				if (events & EPOLLIN)
				{
					on_recv_worker_msg(ctx, netTh);
					while (has_worker_msg(ctx))  // has worker msg
					{
						on_recv_worker_msg(ctx, netTh);
					}
				}	
			}
		else if (type == SOCK_TYPE_TCP_CONN)
		{
			int connRet = -1;
			if (events & EPOLLERR)  // conn failed
				{
					connRet = errno;
				}
			else if (events & EPOLLOUT) 
				{
					connRet = 0;
				}
			else  // exception
			{
				connRet = -3;
				printf(">>>>>> epoll_wait, conn ret exception: id=%d, fd=%d, events=%d\n", sock->id, sock->fd, events);
			}
			on_connect_ret(netTh, ctx, sock->session, sock, sock->worker, connRet, 1);
		}
	}
	return ret;
}

static int do_listen(struct pipes_net_context* net, struct pipes_tcp_server_cfg* cfg, uint64_t from, int* idOut);
static void do_close(struct pipes_net_context* net, int id);
static void do_connect_1(struct pipes_net_thread_context* netTh, 
	struct pipes_net_context* net, int session, 
	struct pipes_tcp_client_cfg* cfg, uint64_t worker);
static void do_connect_2(
	struct pipes_net_thread_context* netTh,
	struct pipes_net_context* net, 
	int id, int fd, int err);
static void on_connect_timeout(
	struct pipes_net_thread_context* netTh,
	struct pipes_net_context* net, 
	int session);
static void do_start_session(struct pipes_net_context* net,
	struct pipes_net_thread_context* netTh,
	int id,
	uint64_t worker);
static void do_enable_write(struct pipes_net_context* net, int id);
static void do_send(struct pipes_net_context* net, 
	int id,
	int off,
	int size,
	void* buf);
static void do_shutdown(struct pipes_net_thread_context* netTh);
static int has_worker_msg(struct pipes_net_context* net)
{
	struct timeval tmwait = { 0, 0 };
	FD_SET(net->sockpair_read, &net->cmd_rfds);
	int ret = select(net->sockpair_read + 1, &net->cmd_rfds, NULL, NULL, &tmwait);
	if (ret == 1)
	{
		return 1;
	}
	return 0;
}
static void on_recv_worker_msg(struct pipes_net_context* net, struct pipes_net_thread_context* netTh)
{
	int fd = net->sockpair_read;
	char* buf = net->tmp_buf;
	int tmp = recv(fd, buf, sizeof(struct req_head), 0);
	struct req_head* head = (struct req_head*)buf;
	switch (head->cmd)
	{
	case NET_CMD_TCP_SEND: 
	{	
		recv(fd, buf + sizeof(struct req_head), sizeof(struct req_send) - sizeof(struct req_head), 0);
		struct req_send* req = (struct req_send*)buf;
		do_send(net, req->id, req->off, req->size, req->buf);
		break;
	}
	case NET_CMD_ENABLE_WRITE:
		{	
			recv(fd, buf + sizeof(struct req_head), sizeof(struct req_enable_write) - sizeof(struct req_head), 0);
			struct req_enable_write* req = (struct req_enable_write*)buf;
			do_enable_write(net, req->id);
			break;
		}
	case NET_CMD_TCP_START:
		{   
			recv(fd, buf + sizeof(struct req_head), sizeof(struct req_start_session) - sizeof(struct req_head), 0);
			struct req_start_session* req = (struct req_start_session*)buf;
			do_start_session(net, netTh, req->id, req->worker);
			break;
		}
	case NET_CMD_TCP_CLOSE: 
		{
			recv(fd, buf + sizeof(struct req_head), sizeof(struct req_close) - sizeof(struct req_head), 0);
			struct req_close* req = (struct req_close*)buf;
			do_close(net, req->id);
			break;
		}
	case NET_CMD_TCP_CONNECT: 
		{
			recv(fd, buf + sizeof(struct req_head), sizeof(struct req_tcp_connect) - sizeof(struct req_head), 0);
			struct req_tcp_connect* req = (struct req_tcp_connect*)buf;
			do_connect_1(netTh, net, req->session, &req->cfg, req->worker);
			break;
		}
	case NET_CMD_GETADDR_RET:
	{
		recv(fd, buf + sizeof(struct req_head), sizeof(struct req_getaddr_ret) - sizeof(struct req_head), 0);    
		struct req_getaddr_ret* req = (struct req_getaddr_ret*)buf;
		do_connect_2(netTh, net, req->id, req->fd, req->err);
		break;
	}
	case NET_CMD_TIMEOUT: {
		recv(fd, buf + sizeof(struct req_head), sizeof(struct req_connect_timeout) - sizeof(struct req_head), 0);
		struct req_connect_timeout* req = (struct req_connect_timeout*)buf;
		on_connect_timeout(netTh, net, req->id);
		break;
	}
	case NET_CMD_TCP_LISTEN: {
		recv(fd, buf + sizeof(struct req_head), sizeof(struct req_tcp_listen) - sizeof(struct req_head), 0);
		struct req_tcp_listen* req = (struct req_tcp_listen*)buf;
		int id = 0;
		int ret = do_listen(net, &req->cfg, req->worker, &id);
		struct net_tcp_listen_ret rsp; 
		if (ret != 0)  // listen failed
		{
			rsp.err = ret;	
		}
		rsp.succ = (ret == 0);
		send_msg_to_worker(netTh, req->worker, req->session, NET_CMD_TCP_LISTEN, id, &rsp, sizeof(rsp));
		break;
	}
	case NET_CMD_SHUTDOWN: {
		do_shutdown(netTh);
		break;
	}
	default: {
		printf("pipes_socket, unknown cmd: %d\n", head->cmd);
	}
	}
}
static void do_shutdown(struct pipes_net_thread_context* netTh)
{
	struct pipes_thread_context* th = (struct pipes_thread_context*)netTh;
	if (th->shutdown_state <= SHUTDOWN_STATE_NO)
	{
		th->shutdown_state = SHUTDOWN_STATE_PROC;
	}
}
static struct send_buffer* new_send_data(void* buf, int off, int size)
{
	struct send_buffer* data = MALLOC(sizeof(struct send_buffer));
	data->buf = buf;
	data->off = off;
	data->size = size;
	data->next = NULL;
	return data;
}
static void add_send_data(struct socket* s, void* buf, int off, int size)
{
	struct send_buffer* data = new_send_data(buf, off, size);
	struct send_list* ls = &s->send_list;
	if (ls->tail == NULL)
	{
		ls->head = data;
	}
	else 
	{
		ls->tail->next = data;
	}
	ls->tail = data;
}
static void do_send(struct pipes_net_context* net, 
	int id, int off, int size, void* buf)
{
	int ret = -1;
	struct socket* s = get_sock_session(net, id);
	do
	{
		if (s->id != id || s->type != SOCK_TYPE_TCP)
		{
			break;
		}
		if (s->closed || s->call_close)
		{
			dec_sending_ref(s);
			break;
		}
		add_send_data(s, buf, off, size);
		if ( !(s->event & EPOLLOUT) ) // has not reg epollout
		{
			s->event = s->event | EPOLLOUT;
			mod_fd(net->fd, s->fd, s->event, s);
		}
		ret = 0;
	} while (0);
	if (ret != 0)  // add failed, free
	{
		FREE(buf);
	}
}
static void do_enable_write(struct pipes_net_context* net, int id)
{
	struct socket* s = get_sock_session(net, id);
	do
	{
		if (s->closed || s->call_close || id != s->id || s->type != SOCK_TYPE_TCP)  // conn has gone
		{
			break;
		}
		if (s->worker == 0) // has not call start
		{
			break;
		}
		if ( !(s->event & EPOLLOUT) )  // has not reg epollout
		{
			s->event = s->event | EPOLLOUT;
			mod_fd(net->fd, s->fd, s->event, s);
		}
	} while (0);
}
static void do_start_session(struct pipes_net_context* net,
	struct pipes_net_thread_context* netTh,
	int id,
	uint64_t worker)
{
	int notifyClosed = 0;
	struct socket* s = get_sock_session(net, id);
	do
	{
		if (s->closed || s->call_close || id != s->id || s->type != SOCK_TYPE_TCP)  // session has gone
		{
			notifyClosed = 1;
			break;
		}
		if (s->worker != 0)  // start session has called
		{
			break;
		}
		s->worker = worker;
		s->event = EPOLLIN;
		int tmp = mod_fd(net->fd, s->fd, s->event, s);    // reg epollin
		if(tmp != 0)
		{
			printf(">>>>>> start_session error, id=%d, fd=%d, err=%d\n", id, s->fd, errno);
		}
	} while (0);
	if (notifyClosed)
	{
		send_msg_to_worker(netTh, worker, 0, NET_CMD_TCP_CLOSE, id, NULL, 0);
	}
}

//
static void on_connect_ret(
	struct pipes_net_thread_context* netTh, 
	struct pipes_net_context* net,
	int session,
	struct socket* sock,
	uint64_t worker,
	int ret,
	int fromEpoll)
{
	if (sock)
	{
		if (fromEpoll)
		{
			del_fd(net->fd, sock->fd);    // del epoll events
		}
	}
	struct net_tcp_connect_ret rsp;
	rsp.ret = ret;
	uint32_t id = 0;
	if (ret == 0)  // connect succ
	{
		id = sock->id;
		sock->type = SOCK_TYPE_TCP;   // change type from conn to tcp
		sock->worker = 0;
		sock->event = 0;
		add_fd(net->fd, sock->fd, sock->event, sock);
	}
	else  // connect failed
	{
		if (sock != NULL)
		{
			force_close(net, sock);
		}
	}
	// notify worker
	send_msg_to_worker(netTh, worker, session, NET_CMD_TCP_CONNECT, id, &rsp, sizeof(rsp));
}
static void on_connect_timeout(
	struct pipes_net_thread_context* netTh,
	struct pipes_net_context* net, 
	int id)
{
	int ret = -1;
	int session = 0;
	uint64_t worker = 0;
	struct socket* sock = get_sock_session(net, id);
	do
	{
		if (sock->id != id || sock->type != SOCK_TYPE_TCP_CONN)  // sock has changed
		{
			break;
		}
		// conn has not established, rsp conn failed
		if(sock->fd > 0)
		{
			close_fd(net->fd, sock->fd);
		}
		session = sock->session;
		worker = sock->worker;
		ret = 0;
	} while (0);
	if (ret == 0)   // notify
	{	
		on_connect_ret(netTh, net, session, sock, worker, ETIMEDOUT, 0);
	}
}
struct getaddrinfo_task
{
	int id;
	int net_fd;
	THREAD_FD net_th;
	struct pipes_tcp_client_cfg cfg;
};
static void thread_getaddrinfo(void* arg)
{	
	// detach
	THREAD_FD fdSelf = pipes_thread_self();
	pipes_thread_detach(fdSelf);
	// getaddrinfo
	struct getaddrinfo_task* task = (struct getaddrinfo_task*)arg;
	struct pipes_tcp_client_cfg* cfg = &task->cfg;
	//
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	char port[16];
	sprintf(port, "%d", cfg->port);
	memset(&ai_hints, 0, sizeof(ai_hints));
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;
	// printf("getaddrinfo start, %s:%d\n", cfg->host, cfg->port);
	int sock = -1;
	int err = getaddrinfo(cfg->host, port, &ai_hints, &ai_list);
	// printf("getaddrinfo done, ret=%d\n", err);
	if (err == 0)  // succ
	{
		struct addrinfo *ai_ptr = NULL;
		for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
			sock = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
			if (sock < 0) {
				continue;
			}
			set_keepalive(sock);
			set_non_blocking(sock);
			set_tcp_nodelay(sock);
			init_tcp_fd(sock);
			
			err = connect(sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
			if (err != 0 && errno != EINPROGRESS) {	// cant reach
				close(sock);
				sock = -1;
				err = errno;
				continue;
			}
			//printf("getaddrinfo succ, ret=%d, errno=%d\n", err, errno);
			break;
		}
	}
	// ret
	int thExist = pipes_thread_exist(task->net_th);
	// printf("getaddrinfo rsp start, thExist=%d, id=%d, fd=%d, err=%d\n", thExist, task->id, sock, err);
	if (thExist == 1)  // net-thread exist, notify
	{
		struct req_getaddr_ret req;
		req.head.cmd = NET_CMD_GETADDR_RET;
		req.id = task->id;
		req.fd = sock;
		req.err = err;
		size_t sz = sizeof(req);
		int ret = send(task->net_fd, &req, sz, 0);
		if (ret < 0)  // send failed, do clear
		{
			if (sock > 0)
			{
				close(sock);
			}
		}
	}
	else  // net-thread gone, do clear
	{
		if (sock > 0)
		{
			close(sock);
		}
	}
	pipes_free(task);
}
static void do_connect_1(
	struct pipes_net_thread_context* netTh,
	struct pipes_net_context* net, 
	int session,
	struct pipes_tcp_client_cfg* cfg, uint64_t worker)
{
	struct socket* sock = NULL;
	do
	{	
		// new sock session
		int id = new_id(net, -1, SOCK_TYPE_TCP_CONN, worker);
		if (id < 1)   // reach max socket session
		{
			printf(">>>>>> do_connect_1 error, reach max socket limit, %d\n", MAX_SOCK_SESSION);
			on_connect_ret(netTh, net, session, NULL, worker, -1, 0);
			break;
		}
		sock = get_sock_session(net, id);
		sock->session = session;
		set_sock_decoder(sock, &cfg->decoder);   // set decoder
		// new thread to getaddrinfo
		THREAD_FD fdSelf = pipes_thread_self();
		size_t sz = sizeof(struct getaddrinfo_task);
		struct getaddrinfo_task* task = pipes_malloc(sz);
		task->id = id;
		task->net_fd = net->sockpair_write;
		task->net_th = fdSelf;
		task->cfg = *cfg;
		THREAD_FD fdChild;
		pipes_thread_start(&fdChild, thread_getaddrinfo, task);
		// set timeout
		if(cfg->conn_timeout > 0)  // set conn timeout
		{
			pipes_api_timeout(netTh->sys_service, cfg->conn_timeout, id, 0);
		}
	} while (0);
}
static void do_connect_2(
	struct pipes_net_thread_context* netTh,
	struct pipes_net_context* net, 
	int id, int fd, int err)
{
	int ret = -1;
	struct socket* s = get_sock_session(net, id);
	do
	{	
		if (s->id != id || s->type != SOCK_TYPE_TCP_CONN)   // gone by timeout
		{	
			break;
		}
		if (fd < 0) // conn failed
		{
			on_connect_ret(netTh, net, s->session, s, s->worker, err, 0);
			break;
		}
		if (err != 0)  // in conn progress
		{
			s->fd = fd;
			s->event = EPOLLOUT;
			add_fd(net->fd, fd, s->event, s);    //reg connect events
		}
		else  // conn succ
		{
			uint64_t worker = s->worker;
			s->type = SOCK_TYPE_TCP;
			s->fd = fd;
			s->worker = 0;
			on_connect_ret(netTh, net, s->session, s, worker, 0, 0);
		}
		ret = 0;
	} while (0);
	if (ret != 0)   // error
	{
		if (fd > 0)  // close fd
		{
			close(fd);
		}
	}
}

static int do_listen(struct pipes_net_context* net, struct pipes_tcp_server_cfg* cfg, uint64_t from, int* idOut)
{ 
	int ret = -1;
	int id = 0;
	int fd = -1;
	do
	{
		fd = pipes_tcp_server(cfg);
		if (fd < 0)  // listen failed
		{
			ret = 1;
			break;
		}
		set_non_blocking(fd);
		id = new_id(net, fd, SOCK_TYPE_LISTEN, from);
		if (id < 1)   // reach max socket session
		{
			close(fd);
			printf(">>>>>> do_listen error, reach max socket limit, %d, fd=%d\n", MAX_SOCK_SESSION, fd);
			ret = 2;
			break;
		}
		struct socket* sock = get_sock_session(net, id);
		set_sock_decoder(sock, &cfg->decoder);    // set decoder
		sock->event = EPOLLIN;
		int tmp = add_fd(net->fd, fd, sock->event, sock);
		//int tmp = add_fd(net->fd, fd, 0, sock);
		//tmp = mod_fd(net->fd, fd, EPOLLIN, sock);
		if (tmp != 0)
		{
			printf(">>>>>> do_listen, opfd error, ret=%d, err=%d\n", tmp, errno);	
		}
		*idOut = id;
		ret = 0;
	} while (0);
	//printf("do_listen done, ret=%d, id=%d, fd=%d\n", ret, id, fd);  // debug
	return ret;
};
static void do_close(struct pipes_net_context* net, int id)
{
	struct socket* s = get_sock_session(net, id);  
	do
	{
		if (id == 4) // debug
		{
			int n = 1;
		}
		if (s->call_close || s->id != id || s->type != SOCK_TYPE_TCP)
		{
			break;
		}
		s->call_close = 1;
		if (s->closed)
		{
			break;
		}
		if ( !(s->event & EPOLLOUT) )  // has not reg epollout
		{
			s->event |= EPOLLOUT;
			mod_fd(net->fd, s->fd, s->event, s);
		}
	} while (0);
}

//
int pipes_net_send_msg(struct pipes_net_context* net, void* data, size_t sz)
{
	if (data)
	{
		int ret = send(net->sockpair_write, data, sz, 0);
		return ret;
	}
	return 0;
}


int pipes_net_shutdown(struct pipes_net_context* net)
{
	struct req_shutdown req;
	req.head.cmd = NET_CMD_SHUTDOWN;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}
int pipes_net_connect_timeout(struct pipes_net_context* net, int id)
{
	struct req_connect_timeout req;
	req.head.cmd = NET_CMD_TIMEOUT;
	req.id = id;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}
int pipes_net_tcp_connect(struct pipes_net_context* net, int session, uint64_t worker, struct pipes_tcp_client_cfg* cfg)
{
	struct req_tcp_connect req;
	req.head.cmd = NET_CMD_TCP_CONNECT;
	req.session = session;
	req.worker = worker;
	req.cfg = *cfg;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}
int pipes_net_tcp_listen(struct pipes_net_context* net, int session, uint64_t worker, struct pipes_tcp_server_cfg* cfg)
{
	struct req_tcp_listen req;
	req.head.cmd = NET_CMD_TCP_LISTEN;
	req.session = session;
	req.worker = worker;
	req.cfg = *cfg;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}
int pipes_net_start_session(struct pipes_net_context* net, uint32_t id, uint64_t worker)
{
	struct req_start_session req;
	req.head.cmd = NET_CMD_TCP_START;
	req.id = id;
	req.worker = worker;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}
static void add_direct_send_data(struct socket* s, void* buf, int off, int size)
{
	struct send_buffer* data = new_send_data(buf, off, size);
	struct direct_send_list* ls = &s->direct_send_list;
	if (ls->tail == NULL)  // no data in list
	{
		ls->head = data;
	}
	else
	{
		ls->tail->next = data;
	}
	ls->tail = data;
}
static int is_socket_invalid(struct socket* s, int id)
{
	if (s->id != id || s->type != SOCK_TYPE_TCP)
	{
		return 1;
	}
	return 0;
}
static int can_direct_send(struct socket* s)
{
	//return 0;  // debug
	if (no_sending_data(s))
	{
		return 1;
	}
	return 0;
}
int pipes_net_tcp_send(struct pipes_net_context* net, uint32_t id, void* buf, int off, int sz, int isTmpBuf)
{
	struct socket* sock = get_sock_session(net, id);
	if (is_socket_invalid(sock, id))
	{
		return 0;
	}
	if (sock->call_close)
	{
		return 0;
	}
	if (can_direct_send(sock) && socket_trylock(sock))
	{
		if (is_socket_invalid(sock, id))
		{
			socket_unlock(sock);
			return 0;
		}
		if (can_direct_send(sock))  // can direct send
		{
			//printf("direct send begin, id=%d, size=%d\n", id, sz);  // debug
			int tmp = send(sock->fd, buf + off, sz, 0);
			if (tmp == -1 && errno != EAGAIN && errno != EWOULDBLOCK) // conn down, send failed
			{
				socket_unlock(sock);
				return 0;
			}
			if (tmp > 0)  // has send some data
			{
				add_send_bytes(sock, tmp);
				if (tmp >= sz)  // send done
				{	
					socket_unlock(sock);
					return 1;
				}
				// send partly or should wait, add to direct-send-list
				off += tmp;
				sz -= tmp;
			}
			if (isTmpBuf)  //
			{
				void* newBuf = MALLOC(sz);
				memcpy(newBuf, buf + off, sz);
				buf = newBuf;
				off = 0;
			}
			//printf("direct partly send, id=%d, left=%d\n", id , sz); 
			add_direct_send_data(sock, buf, off, sz);
			socket_unlock(sock);
			// inc sending ref-cnt
			inc_sending_ref(sock);  
			// enable write event
			struct req_enable_write req;
			req.head.cmd = NET_CMD_ENABLE_WRITE;
			req.id = id;
			pipes_net_send_msg(net, &req, sizeof(req));
			return 2;
		}
		socket_unlock(sock);
	}
	if (ATOM_CAS(&sock->closed, 1, 1))  // has closed
	{
		return 0;	
	}
	//printf("send to net-thread, id=%d, size=%d\n", id, sz);  // debug
	// inc sending ref-cnt
	inc_sending_ref(sock);   
	// send to net thread
	if(isTmpBuf) 
	{
		void* newBuf = MALLOC(sz);
		memcpy(newBuf, buf + off, sz);
		buf = newBuf;
		off = 0;
	}
	struct req_send req;
	req.head.cmd = NET_CMD_TCP_SEND;
	req.id = id;
	req.off = off;
	req.size = sz;
	req.buf = buf;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 3;
}
int pipes_net_close_tcp(struct pipes_net_context* net, uint32_t id)
{
	struct req_close req;
	req.head.cmd = NET_CMD_TCP_CLOSE;
	req.id = id;
	int ret = pipes_net_send_msg(net, &req, sizeof(req));
	return 1;
}


// ==== net-msg wrapper
void* alloc_net_msg(int cmd, uint32_t id, size_t cap, size_t* szActual)
{
	*szActual = cap + 1 + 4;   // cmd(1) + id(4)
	void* out = ADAPTER_MALLOC(*szActual);
	*(unsigned char*)out = cmd;
	*(uint32_t*)(out + 1) = id;
	return out;
}
void* alloc_net_read_buf(int cmd, uint32_t id, unsigned char readFlag, size_t bufCap, size_t* szActual)
{
	// cmd(1)+flag(1), size(2), id(4), next(sizeof(struct net_readblock_head*))
	size_t szHead = sizeof(struct net_readblock_head);
	*szActual = bufCap + szHead;   
	void* out = ADAPTER_MALLOC(*szActual);
	struct net_readblock_head* head = (struct net_readblock_head*)out;
	((unsigned char*)head)[0]  = cmd;
	*((int*)head->meta) = id;
	head->next = NULL;
	return out;
}
void* wrap_net_msg(int cmd, uint32_t id, void* ori, size_t szOri, size_t* szOut)
{
	if (ori == NULL)
	{
		szOri = 0;
	}
	*szOut = szOri + 1 + 4;  // cmd + id
	void* out = ADAPTER_MALLOC(*szOut);
	*(unsigned char*)out = cmd;
	*(uint32_t*)(out + 1) = id;
	if (ori)
	{
		memcpy(out + 1 + 4, ori, szOri);
	}
	return out;
}
void* unwrap_net_msg(void* msg, size_t szMsg, int* cmd, uint32_t* id, size_t* szPayload)
{
	if (cmd)
	{
		*cmd = *(unsigned char*)msg;
	}
	if (id)
	{
		*id = *(uint32_t*)(msg + 1);
	}
	size_t off = szMsg - 1 - 4;
	if (off > 0)  // has payload
	{
		if (szPayload)
		{
			*szPayload = off;
		}
		return msg + 1 + 4;
	}
	if (szPayload)
	{
		*szPayload = 0;
	}
	return NULL;
}
int gain_net_msg_head(void* msg, uint32_t* id)
{
	int cmd = ((unsigned char*)msg)[0];
	if (id)
	{
		if (cmd == NET_CMD_TCP_RECV)  // has read-flag
		{
			*id = *(uint32_t*)(msg + 4);
		}
		else
		{
			*id = *(uint32_t*)(msg + 1);
		}
	}
	
	return cmd;
}
int gain_net_read_packsize(void* msg, int lenBytes)
{
	if (lenBytes == 4)  //
	{
		return *(uint32_t*)(msg + 6);
	}
	else
	{
		return *(uint16_t*)(msg + 6);
	}
}

#else


#endif // SYS_IS_LINUX




