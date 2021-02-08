

#include "pipes_start.h"

#include "pipes.h"

#include "pipes_thread.h"
#include "pipes_mq.h"
#include "pipes_handle.h"
#include "pipes_server.h"
#include "pipes_socket_thread.h"
#include "timing_wheel.h"

#include "atomic.h"

#include "cJSON.h"
//
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define MAIL_BOX_SIZE 64
#define HANDLE_MGR_CAP 256
#define LAUNCH_SERVICE_NAME ".launch_service"
#define NET_HANDLE_MGR_CAP 1024

static void* thread_net(void* arg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)arg;
	struct pipes_net_thread_context* netCtx = (struct pipes_net_thread_context*)arg;
	struct pipes_global_context* global = thCtx->global;
	//
	pipes_net_thread_init(netCtx->net_ctx);
	ATOM_INC(&(global->thread_init_cnt));
	while (ATOM_ADD(&(global->thread_init_cnt), 0) < global->all_thread_num)
	{
	         
	}
	printf("thread_net start, thread_index=%d\n", thCtx->idx);
	while (thCtx->loop)
	{
		net_thread_tick(netCtx);
	}
	printf("thread_net quit, thread_index=%d\n", thCtx->idx);
	destroy_net_thread(netCtx);
	return NULL;
}
static void* thread_timer(void* arg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)arg;
	struct pipes_timer_thread_context* tmCtx = (struct pipes_timer_thread_context*)arg;
	struct pipes_global_context* global = thCtx->global;
	//
	ATOM_INC(&(global->thread_init_cnt));
	while (ATOM_ADD(&(global->thread_init_cnt), 0) < global->all_thread_num)
	{
	         
	}
	printf("thread_timer start, thread_index=%d\n", thCtx->idx);
	while (thCtx->loop)
	{
		timer_thread_tick(tmCtx);
	}
	printf("thread_timer quit, thread_index=%d\n", thCtx->idx);
	destroy_timer_thread(tmCtx);
	
	return NULL;
}
static void* thread_worker(void* arg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)arg;
	struct pipes_worker_thread_context* wkCtx = (struct pipes_worker_thread_context*)arg;
	struct pipes_global_context* global = thCtx->global;
	//
	ATOM_INC(&(global->thread_init_cnt));
	while (ATOM_ADD(&(global->thread_init_cnt), 0) < global->all_thread_num)
	{
	         
	}
	printf("thread_worker start, thread_index=%d\n", thCtx->idx);
	//
	cJSON_InitHooks(NULL);
	//
	if(thCtx->idx == 0)   // use thread-0 for boot service
	{
		void* bootAdapter = global->adapter_cfg.boot_create_cb(thCtx->idx);
		uint64_t handle = 0;
		int bootRet = pipes_api_create_service(wkCtx->sys_service, thCtx->idx, 0, bootAdapter, LAUNCH_SERVICE_NAME, &handle);
	}
	//
	while (thCtx->loop)
	{
		worker_thread_tick(wkCtx);     
	}
	printf("thread_worker quit, thread_index=%d\n", thCtx->idx);
	//deatroy worker ctx
	destroy_worker_thread(wkCtx);
	//
	return NULL;
}

static void init_thread_context(struct pipes_thread_context* ctx, struct pipes_global_context*g, int idx)
{
	ctx->idx = idx;
	ctx->global = g;
	ctx->loop = 1;
	ctx->local_buf = NULL;
	ctx->local_buf_size = 0;
	ctx->shutdown_state = 0;   // init shutdown-state
	ctx->msg_num = 0;
	ctx->mem = 0;
	ctx->service_num = 0;
	//
	ctx->has_send_thread_cnt = 0;
	size_t sz = sizeof(int) * g->msg_thread_num;
	ctx->has_send_thread_flags = pipes_malloc(sz);
	memset(ctx->has_send_thread_flags, 0, sz);
}
static void init_msg_thread_context(struct pipes_msg_thread_context* ctx, struct pipes_global_context*g, int idx)
{
	init_thread_context((struct pipes_thread_context*)ctx, g, idx);
	// init rw-queue
	size_t sz = sizeof(struct swap_message_queue*) * g->all_thread_num;// g->msg_thread_num;
	ctx->mailboxes = pipes_malloc(sz);
	memset(ctx->mailboxes, 0, sz);
	int i;
	for (i = 0; i < g->all_thread_num; ++i)
	{
		ctx->mailboxes[i] = pipes_smq_create(MAIL_BOX_SIZE);
	}
	// init mutex&cond
	pipes_thread_mutex_init(&ctx->mutex);
	pipes_thread_cond_init(&ctx->cond);
}
static void init_timer_thread_context(struct pipes_timer_thread_context* ctx, struct pipes_global_context* g)
{
	init_msg_thread_context((struct pipes_msg_thread_context*)ctx, g, g->idx_timer);
	//
	ctx->cur_mailbox = 0;
	// create timer
	uint32_t tickDur = 10;
	uint32_t wheelSize = 100;
	struct timeval tmVal;
	pipes_time_now(&tmVal);
	uint64_t tmStart = pipes_time_toms(&tmVal);
	uint32_t taskPoolCap = 1000;
	uint32_t delayQueueCap = 1000;
	ctx->timer = tmwheel_create_timer(
		tickDur, wheelSize, tmStart, 
		on_expire_callback, pipes_malloc, pipes_free, 
		taskPoolCap, delayQueueCap);
	//
	init_timer_task_pool(ctx);
	// init sys service
	size_t sz = sizeof(struct pipes_service_context);
	ctx->sys_service = pipes_malloc(sz);
	memset(ctx->sys_service, 0, sz);
	ctx->sys_service->thread = (struct pipes_thread_context*)ctx;
	ctx->sys_service->handle = pipes_handle_reserve_handle(g->harbor, g->idx_timer);
}
static void init_net_thread_context(struct pipes_net_thread_context* ctx, struct pipes_global_context* g)
{
	init_thread_context((struct pipes_thread_context*)ctx, g, g->idx_net);
	// init mutex&cond
	pipes_thread_mutex_init(&ctx->mutex);
	pipes_thread_cond_init(&ctx->cond);
	//
	ctx->net_ctx = pipes_net_create();
	g->net_ctx = ctx->net_ctx;
	// init sys service
	size_t sz = sizeof(struct pipes_service_context);
	ctx->sys_service = pipes_malloc(sz);
	memset(ctx->sys_service, 0, sz);
	ctx->sys_service->thread = (struct pipes_thread_context*)ctx;
	ctx->sys_service->handle = pipes_handle_reserve_handle(g->harbor, g->idx_net);
}
static void init_worker_thread_context(struct pipes_worker_thread_context* ctx, struct pipes_global_context* g, int idx)
{
	init_msg_thread_context((struct pipes_msg_thread_context*)ctx, g, idx);
	
	ctx->handle_mgr = pipes_handle_new_storage(
		g->harbor,
		HANDLE_MGR_CAP,
		pipes_get_context_handle, 
		pipes_set_context_handle,
		pipes_malloc,
		pipes_free,
		sizeof(struct pipes_service_context*));
	
	// init worker queue
	size_t sz = sizeof(struct worker_message_queue);
	ctx->worker_msg_queue = pipes_malloc(sz);
	memset(ctx->worker_msg_queue, 0, sz);
	//
	ctx->cur_mailbox = 0;
	// init sys service
	sz = sizeof(struct pipes_service_context);
	ctx->sys_service = pipes_malloc(sz);
	memset(ctx->sys_service, 0, sz);
	ctx->sys_service->thread = (struct pipes_thread_context*)ctx;
	ctx->sys_service->handle = pipes_handle_reserve_handle(g->harbor, idx);
	// init worker-thread local buf
	size_t szLocalBuf = 4096;
	ctx->msg_thread_ctx.thread_ctx.local_buf = pipes_malloc(szLocalBuf);
	ctx->msg_thread_ctx.thread_ctx.local_buf_size = szLocalBuf;
}

static void start_thread(struct pipes_global_context* global)
{
	int i;
	int workerNum = global->worker_thread_num;
	
	THREAD_FD fds[global->all_thread_num];
	
	// create net context
	struct pipes_net_thread_context ctxNet;
	memset(&ctxNet, 0, sizeof(ctxNet));
	init_net_thread_context(&ctxNet, global);
	global->threads[global->idx_net] = (struct pipes_thread_context*)(&ctxNet);
	
	// create timer context
	struct pipes_timer_thread_context ctxTimer;
	memset(&ctxTimer, 0, sizeof(ctxTimer));
	init_timer_thread_context(&ctxTimer, global);
	global->threads[global->idx_timer] = (struct pipes_thread_context*)(&ctxTimer);
	
	// create workers context
	struct pipes_worker_thread_context arrCtx[workerNum];
	memset(arrCtx, 0, sizeof(arrCtx));
	for (i = 0; i < workerNum; ++i)
	{
		struct pipes_worker_thread_context* ctx = &arrCtx[i];
		init_worker_thread_context(ctx, global, i);
		global->threads[i] = (struct pipes_thread_context*)ctx;
	}
	
	// start thread net
	pipes_thread_start(&fds[global->idx_net], thread_net, &ctxNet);
	
	// start thread_timer
	pipes_thread_start(&fds[global->idx_timer], thread_timer, &ctxTimer);
	
	//start thread_worker
	for (i = 0; i < workerNum; ++i)
	{
		pipes_thread_start(&fds[i], thread_worker, &arrCtx[i]);
	}
	// all thread join
	for(i = 0 ; i < global->all_thread_num ; ++i)
	{
		pipes_thread_join(&fds[i]);
	}
	fprintf(stdout, "all thread quit\n");
	//
	destroy_global(global);
}

static void check_bigendian(struct pipes_global_context* g)
{
	unsigned char buf[2];
	buf[0] = 1;
	buf[1] = 0;
	if (*((uint16_t*)buf) == 1)
	{
		g->is_bigendian = 0;
	}
	else
	{
		g->is_bigendian = 1;
	}
}
int pipes_start(struct pipes_start_config* config)
{
	int ret = -1;
	do
	{	
		struct pipes_global_context global;
		memset(&global, 0, sizeof(global));
		check_bigendian(&global);
		global.harbor = config->harbor;
		global.worker_thread_num = config->worker_num;
		global.all_thread_num = global.worker_thread_num + CORE_THREAD_NUM;
		global.msg_thread_num = global.worker_thread_num + 1;    //workers & timer
		
		global.idx_timer = global.worker_thread_num;
		global.idx_net = global.worker_thread_num + 1;
		
		global.threads = pipes_malloc(sizeof(struct pipes_thread_context*) * global.all_thread_num);
		
		global.thread_init_cnt = 0;
		
		global.handle_mgr = pipes_handle_new_storage(global.harbor,
			HANDLE_MGR_CAP,
			pipes_get_context_handle,
			pipes_set_context_handle,
			pipes_malloc,
			pipes_free,
			sizeof(struct pipes_service_context*));
		//
		memcpy(&global.adapter_cfg, &config->adapter_cfg, sizeof(global.adapter_cfg));
		// init task-thread-pool
		SPIN_INIT(&global.th_pool);
		
		// start thread
		start_thread(&global);
		
		ret = 0;
	} while (0);
	return ret;
}






