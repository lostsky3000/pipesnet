#ifndef PIPES_API_H
#define PIPES_API_H

#include <stdint.h>
#include <stddef.h>
#include "pipes_tcp.h"

// sys ready callback
struct pipes_service_context;

typedef void*(*pipes_on_bootservice_create)(int curThread);
typedef int(*pipes_service_init_callback)(void* adapter, struct pipes_service_context* service);
typedef void(*pipes_service_exit_callback)(void* adapter);
typedef void(*pipes_destroy_adapter)(void* adapter);
typedef int(*pipes_message_callback)(  // message callback
	struct pipes_service_context* ctx,
	int type,
	int session, 
	uint64_t source,
	void* data,
	uint32_t size);

struct pipes_adapter_config
{
	pipes_on_bootservice_create boot_create_cb;
	pipes_service_init_callback service_init_cb;
	pipes_service_exit_callback service_exit_cb;
	pipes_message_callback msg_cb;
	pipes_destroy_adapter destroy_adapter;
};

//
int pipes_api_create_service(
	struct pipes_service_context*caller,
	int toThread,
	int session,
	void* adapter, 
	const char * name, 
	uint64_t* handle);

int pipes_api_exit_service(
	struct pipes_service_context*caller
	);

struct pipes_message;
int pipes_api_redirect(
	struct pipes_service_context*caller,
	uint64_t to, 
	struct pipes_message* msg);

struct pipes_global_context;
int pipes_api_send_sys(
	struct pipes_global_context* g,
	int curThread,
	uint64_t from,
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size
	);
int pipes_api_send(
	struct pipes_service_context*caller,  
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size);
int pipes_api_send_priority(
	struct pipes_service_context*caller,  
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size);

int pipes_api_timeout(struct pipes_service_context*caller, uint32_t delay, int session, uint64_t lastCnt);

uint64_t pipes_api_handle(struct pipes_service_context* ctx);

int pipes_api_corethreadnum();

void pipes_api_shutdown(struct pipes_service_context* caller);

void* pipes_api_get_adapter(struct pipes_service_context*);

// api for net
int pipes_api_tcp_listen(struct pipes_service_context* caller, int session, struct pipes_tcp_server_cfg* cfg);

int pipes_api_tcp_connect(struct pipes_service_context* caller, int session, struct pipes_tcp_client_cfg * cfg);

int pipes_api_net_timeout(struct pipes_service_context* caller, int session);

int pipes_api_tcp_session_start(struct pipes_service_context* caller, uint32_t id, uint64_t source);
int pipes_api_tcp_close(struct pipes_service_context* caller, uint32_t id);

// msg type|size   encode & decode
uint32_t pipes_api_enc_msgtype(int msgType, uint32_t msgSize);
int pipes_api_dec_msgtype(uint32_t msgSize);
uint32_t pipes_api_msg_sizeraw(uint32_t msgSize);

typedef void(*fn_post_task)(void* param, struct pipes_global_context*g);
int pipes_api_post_task(struct pipes_global_context* g, void* param, fn_post_task fn);
int pipes_api_close_thread_pool(struct pipes_global_context* g);

//
struct pipes_thread_context;
uint64_t pipes_api_gethandle_byname(struct pipes_service_context*, const char* name);
struct pipes_thread_context* pipes_api_getthread(struct pipes_service_context*);
void* pipes_api_threadlocal_malloc(struct pipes_thread_context* thread, size_t szRequire, size_t* szActual);
void* pipes_api_threadlocal_realloc(
	struct pipes_thread_context* thread,
	void* ptr,
	size_t szUsed,
	size_t szNewPrefer, 
	size_t szNewMin, 
	size_t* szNewActual);
void pipes_api_threadlocal_free(struct pipes_thread_context* thread, void* ptr);


#endif 







