
#ifndef PIPES_SOCKET_THREAD_H
#define PIPES_SOCKET_THREAD_H

#include "pipes.h"
#include "pipes_socket.h"

struct net_session_context;



struct net_tcp_session_start
{
	uint64_t source;
};
struct net_tcp_close
{	
	uint32_t handle;
};

struct pipes_tcp_context;
void* net_on_accept(struct pipes_tcp_context* tcp, const char* host, int port, void* ud);
char* net_on_prepare_readbuf(int* szRead, void*ud);
int net_on_tcp_recv(char* buf, int read, void* ud);
void net_on_tcp_close(void* ud);


int proc_net_thread_msg(struct pipes_net_thread_context* ctx, struct pipes_message* msg);

//
uint64_t net_get_session_handle(void* ctx);
void net_set_session_handle(void* ctx, uint64_t handle);

//
struct sock_session_context;
void free_sock_session(struct sock_session_context* ss);
void retain_sock_session(struct sock_session_context* ss);


#endif // !PIPES_SOCKET_THREAD_H




