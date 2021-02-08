
#ifndef PIPES_TCP_H
#define PIPES_TCP_H

#include "pipes_macro.h"
#include <stdint.h>

#ifdef SYS_IS_LINUX
#define PIPES_SOCK_FD int
#else
#define PIPES_SOCK_FD int
#endif // SYS_IS_LINUX

#define TCP_DECODE_RAW 0
#define TCP_DECODE_FIELD_LENGTH 1

struct tcp_decoder
{
	int type;
	int val1;
	int maxlength;
};

struct pipes_tcp_server_cfg
{
	struct tcp_decoder decoder;
	int port;
	int backlog;
	char host[20];
};

struct pipes_tcp_client_cfg
{
	struct tcp_decoder decoder;
	int port;
	uint32_t conn_timeout;
	char host[252];
};

PIPES_SOCK_FD pipes_tcp_server(struct pipes_tcp_server_cfg* cfg);

PIPES_SOCK_FD pipes_tcp_socket();

void pipes_tcp_close(PIPES_SOCK_FD fd);

#endif // !PIPES_TCP_H




