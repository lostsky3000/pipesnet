
#ifndef PIPES_H
#define PIPES_H

#include "pipes_malloc.h"
#include "pipes_handle.h"

#include "pipes_thread.h"
#include "timing_wheel.h"
#include "pipes_api.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define CORE_THREAD_NUM 2


// msg-type
#define PMSG_TYPE_DATA_RAW 0
#define PMSG_TYPE_DATA_STR 1
#define PMSG_TYPE_INIT_SERVICE 10
#define PMSG_TYPE_INIT_SERVICE_RESP 11
#define PMSG_TYPE_EXIT_SERVICE 12
#define PMSG_TYPE_FINAL_SERVICE 13

#define PMSG_TYPE_TIMEOUT 20
#define PMSG_TYPE_SHUTDOWN 21 
#define PMSG_TYPE_NET 22

#define PMSG_TYPE_RET_ERROR 30

#define PMSG_TYPE_CUSTOM_BEGIN 64

#define DEFAULT_MSG_PROCESSOR ".LPPS_DEF_MSG_HANDLER"
#define LOGGER_NAME ".LPPS_LOGGER"


#define THREAD_TMPBUF_LEN 2048

struct pipes_message
{
	int session;
	uint32_t size;
	void* data;
	uint64_t source;
	uint64_t dest;         
};

struct pipes_global_context;
struct swap_message_queue;


struct pipes_thread_context
{
	int idx;
	int loop;
	int shutdown_state;
	//
	volatile int service_num;
	//
	int has_send_thread_cnt;
	int* has_send_thread_flags;
	struct pipes_global_context* global;
	//
	void* local_buf;
	size_t local_buf_size;
	//
	int64_t msg_num;
	//
	int64_t mem;
	//
	char tmp_buf[THREAD_TMPBUF_LEN];
};

struct pipes_msg_thread_context
{
	struct pipes_thread_context thread_ctx;
	struct swap_message_queue ** mailboxes;
	THREAD_MUTEX mutex;
	THREAD_COND cond;
	//
	//int has_send_thread_cnt;
	//int* has_send_thread_flags;
	//
	
};

struct pipes_net_thread_context
{
	struct pipes_thread_context thread_ctx;
	THREAD_MUTEX mutex;
	THREAD_COND cond;
	struct pipes_net_context* net_ctx;
	struct pipes_service_context* sys_service;
};
struct timer_task_wrap;
struct pipes_timer_thread_context
{
	struct pipes_msg_thread_context msg_thread_ctx;
	struct tw_timer* timer;
	//
	int cur_mailbox;
	//
	struct timer_task_wrap* task_pool;
	uint32_t pool_cap;
	int* free_pool;
	int free_num;
	//
	struct pipes_service_context* sys_service;
};

struct pipes_worker_thread_context
{
	struct pipes_msg_thread_context msg_thread_ctx;
	
	struct handle_storage* handle_mgr;
	
	struct worker_message_queue* worker_msg_queue;
	int cur_mailbox;
	struct pipes_service_context* sys_service;
	//
	struct pipes_service_context* destroy_head;
	struct pipes_service_context* destroy_tail;
};

struct message_queue;
struct pipes_service_context
{
	int has_exit;
	void* adapter;
	char* name;
	struct message_queue* queue;
	struct pipes_thread_context* thread;
	struct pipes_service_context* next;
	uint64_t handle;
	int64_t msg_proc;
};

struct pipes_net_context;
struct pipes_thread_pool
{
	struct spinlock lock;
	int closed;
	int call_close;
	int exec_th_num;
};
struct pipes_global_context
{
	int harbor;
	int all_thread_num;
	int msg_thread_num;
	int worker_thread_num;
	int idx_timer;
	int idx_net;
	int thread_init_cnt;
	int shutdown_flag;
	int is_bigendian;
	struct pipes_thread_context** threads;
	struct handle_storage* handle_mgr;
	struct pipes_adapter_config adapter_cfg;
	//
	struct pipes_net_context* net_ctx;
	struct pipes_thread_pool th_pool;
};



#endif 


