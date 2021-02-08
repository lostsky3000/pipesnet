
#include "pipes_tcp.h"

#ifdef SYS_IS_LINUX

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

PIPES_SOCK_FD pipes_tcp_server(struct pipes_tcp_server_cfg* cfg)
{
	int sock = -1;
	do
	{
		struct sockaddr_in addrBind;
		memset(&addrBind, 0, sizeof(addrBind));
		addrBind.sin_family = AF_INET;
		int ret = inet_pton(AF_INET, cfg->host, &addrBind.sin_addr);
		if (ret == 0)
		{
			printf("parse tcp addr failed: %s:%d, %d\n", cfg->host, cfg->port, errno);
			break;
		}
		addrBind.sin_port = htons(cfg->port);
		sock = socket(PF_INET, SOCK_STREAM, 0);
		if (sock < 0)
		{
			printf("socket failed: %s:%d, %d\n", cfg->host, cfg->port, errno);
			break;
		}
		// bind
		ret = bind(sock, (struct sockaddr*)&addrBind, sizeof(addrBind));
		if (ret < 0)
		{
			printf("bind failed: %s:%d, %d\n", cfg->host, cfg->port, errno);
			pipes_tcp_close(sock);
			sock = -1;
			break;
		}
		// listen
		ret = listen(sock, cfg->backlog);
		if (ret < 0)
		{
			printf("listen failed: %s:%d, %d\n", cfg->host, cfg->port, errno);
			pipes_tcp_close(sock);
			sock = -1;
			break;
		}
	} while (0);
	return sock;
}
PIPES_SOCK_FD pipes_tcp_socket()
{
	int sock = -1;
	do
	{
		sock = socket(PF_INET, SOCK_STREAM, 0);
		if (sock < 0)
		{
			printf("tcp socket failed: \n", errno);
			break;
		}
	} while (0);
	return sock;
}
void pipes_tcp_close(PIPES_SOCK_FD fd)
{
	close(fd);
}

#else


#endif // SYS_IS_LINUX



/*
void clientTest()
{
	const char* pDst = "192.168.123.7";
	int portDst = 10086;
	//
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		printf("socket err: %d\n", errno);
		return;
	}
	struct sockaddr_in inAddr;
	bzero(&inAddr, sizeof(inAddr));
	inAddr.sin_family = AF_INET;
	inet_pton(AF_INET, pDst, &inAddr.sin_addr);
	inAddr.sin_port = htons(portDst);
	printf("start conn %s:%d\n", pDst, portDst);
	int ret = connect(sock, (struct sockaddr*)&inAddr, sizeof(inAddr));
	if (ret < 0)
	{
		printf("conn failed: %s:%d\n", pDst, portDst);
		return;
	}
	//
	char bufSend[128];
	sprintf(bufSend, "req from client");
	send(sock, bufSend, strlen(bufSend), 0);
	
	char bufRecv[128];
	ret = recv(sock, bufRecv, sizeof(bufRecv), 0);
	if (ret <= 0)
	{
		printf("recv err: %d\n", errno);
	}
	else
	{
		printf("recv succ: %s\n", bufRecv);
	}
	
	printf("conn close\n");
	
	close(sock);
} 
*/



