
#include "pipes_server.h"

#include "pipes.h"
#include "pipes_mq.h"
#include "pipes_thread.h"
#include "pipes_time.h"
#include "pipes_api.h"
#include "atomic.h"
#include "pipes_socket.h"
#include "pipes_adapter.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#define TIMER_TASK_POOL 1000
#define TIMER_WAIT_MS 6000
#define SERVICE_MQ_CAP 64
#define BOOT_SERVICE_THREAD 0    //use thread-0 for boot-service

#define WORKER_WAIT_MS 5000

//
#define THREAD_LOCAL_MEM_INCR_STEP 1024*8

struct timer_task_wrap
{
	int idx;
	struct pipes_timer_thread_context* ctx;
	int last_cnt;
	int session;
	uint32_t src_thread;
	uint64_t src_id;
	uint64_t expiration;
	uint64_t dur;
};
struct create_service_tmp
{
	char* name;
	void* adapter;
};

static void destroy_pipes_msg(struct pipes_message* msg)
{
	if (msg->data)
	{
		pipes_free(msg->data);
		msg->data = NULL;
	}
}
static void destroy_service_msg(struct pipes_message* msg)
{
	if (msg->data)
	{
		ADAPTER_FREE(msg->data);
		msg->data = NULL;
	}
}
static void proc_service_notar_msg(struct pipes_worker_thread_context* wctx, struct pipes_message* m)
{
	if (m->session > 0)  // has session, but tar not found, use default ret
	{
		size_t szStr = 0;
		void* errStr = ADAPTER_COPY_STR("dest service not found", &szStr);
		pipes_api_send(wctx->sys_service, m->source, PMSG_TYPE_RET_ERROR, -m->session, errStr, szStr);
	}
	//
	destroy_service_msg(m);
	/*
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)wctx;
	struct pipes_global_context* global = thCtx->global;
	char tmpBuf[32]; 
	sprintf(tmpBuf, "%s_%d", DEFAULT_MSG_PROCESSOR, thCtx->idx);
	uint64_t handleDestroy = pipes_handle_findname(tmpBuf, global->handle_mgr);
	if (handleDestroy != 0)  // adapter has default msg processor
	{
		pipes_api_redirect(wctx->sys_service, handleDestroy, m);
	}
	else
	{
		destroy_service_msg(m);
	}*/
}
static void proc_service_msg(struct pipes_worker_thread_context* wctx, struct pipes_service_context* sctx,
				struct pipes_message*m)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)wctx;
	if (thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)  // in shutdown proc, not proc msg
	{
		destroy_service_msg(m);
		return;
	}
	if (sctx->adapter == NULL)  // has no adapter
	{
		destroy_service_msg(m);
		return;
	}
	if (sctx->has_exit)  // service has exit
	{
		proc_service_notar_msg(wctx, m);
		return;
	}
	struct pipes_global_context* global = ((struct pipes_thread_context*)wctx)->global;
	int type = pipes_api_dec_msgtype(m->size);
	int ret = global->adapter_cfg.msg_cb(sctx, type, m->session, m->source, m->data, pipes_api_msg_sizeraw(m->size));
	if (!ret)  // msg not consume, destroy
	{
		destroy_service_msg(m);
	}
}

static void add_msg_to_service(struct pipes_worker_thread_context* ctx, int type, struct pipes_message* msg)
{
	struct pipes_service_context* sctx = pipes_handle_grab_unsafe(msg->dest, ctx->handle_mgr);
	if (sctx)
	{
		pipes_mq_push_unsafe(sctx->queue, msg);
		//try to add worker-msg-queue
		int ret = pipes_worker_mq_push(ctx->worker_msg_queue, sctx->queue);
		/*
		printf("add_msg_to_service, ret=%d, workerMqNum=%d, scrxMqSz=%d\n", ret, 
			ctx->worker_msg_queue->queue_num,
			pipes_mq_size_unsafe(sctx->queue)
			);  // debug
			*/
		struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
		++thCtx->msg_num;
	}
	else  // service-ctx not found
		{
			proc_service_notar_msg(ctx, msg);
		}
}
static void destroy_service(struct pipes_service_context* service)
{
	struct pipes_worker_thread_context* wkCtx = (struct pipes_worker_thread_context*)service->thread;
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)wkCtx;
	//
	if(wkCtx)
	{
		// unregister from localthread handle-mgr
		pipes_handle_retire_unsafe(service->handle, wkCtx->handle_mgr);
		// unregister from global handle-mgr
		pipes_handle_retire(service->handle, thCtx->global->handle_mgr);
	}
	//
	if (service->adapter)
	{
		thCtx->global->adapter_cfg.service_exit_cb(service->adapter);
		service->adapter = NULL;
	}
	// destroy service
	service->name = NULL;
	if (service->queue) // destroy msg-queue
	{
		int msgNum = pipes_mq_size_unsafe(service->queue);
		struct pipes_message msg;
		while (pipes_mq_pop_unsafe(service->queue, &msg))  // has msg
		{
			destroy_service_msg(&msg);
		}
		pipes_free(service->queue);
		service->queue = NULL;
		//
		thCtx->msg_num -= msgNum;   // update thread msg-num
	}
	pipes_free(service);
}
static void destroy_create_data(struct pipes_message* msg, struct pipes_global_context* global)
{
	struct create_service_tmp* data = msg->data;
	if (data)
	{
		global->adapter_cfg.destroy_adapter(data->adapter);
	}
	msg->data = NULL;
}
static int do_init_service(struct pipes_worker_thread_context* ctx, struct pipes_message* msg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_global_context* global = thCtx->global;
	struct pipes_service_context* service = pipes_handle_grab(msg->dest, global->handle_mgr);
	if (service == NULL)
	{
		printf("init service error, handle not found: %ld\n", msg->dest);
		destroy_create_data(msg, global);
		return 1;
	}
	//
	if(thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)  // in shutdown proc
	{
		//do clean stuff
		destroy_create_data(msg, global);
		destroy_service(service);
		return 2;
	}
	struct create_service_tmp* cData = msg->data;
	service->thread = (struct pipes_thread_context*)ctx;
	service->adapter = cData->adapter;
	service->name = cData->name;
	service->has_exit = 0;
	service->queue = pipes_mq_create_cap(SERVICE_MQ_CAP);
	service->queue->in_parent = MQ_NOTIN_PARENT;
	service->queue->udata = service;
	service->msg_proc = 0;
	destroy_pipes_msg(msg);
	// notify adapter service-init
	int ret = global->adapter_cfg.service_init_cb(service->adapter, service);
	if (!ret)  // init adapter failed, destroy service
	{
		destroy_service(service);
		return 3;
	}
	// add to local-thread handle-mgr
	int addRet = pipes_handle_add_unsafe(service, service->handle, ctx->handle_mgr);
	assert(addRet);  // should not exist before add
	if(service->name) // has name, add to localthread handle-mgr
	{
		const char* nameTmp = pipes_handle_namehandle_unsafe(service->handle, service->name, ctx->handle_mgr);
		assert(nameTmp != NULL);
	}
	//
	++thCtx->service_num;  // increase service num
	return 0;
}

static void shutdown_start(struct pipes_thread_context* ctx)
{
	if (ctx->shutdown_state <= SHUTDOWN_STATE_NO)
	{
		ctx->shutdown_state = SHUTDOWN_STATE_PROC;
	}
}
static void add_msg_to_worker(struct pipes_worker_thread_context* ctx, struct pipes_message* msg)
{
	int type = pipes_api_dec_msgtype(msg->size);
	switch (type)
	{
	case PMSG_TYPE_INIT_SERVICE: {
			// init service
			int ret = do_init_service(ctx, msg);
			if (msg->session > 0)  // need ret
			{
				pipes_api_send(ctx->sys_service, msg->source, PMSG_TYPE_INIT_SERVICE_RESP, -msg->session, NULL, ret == 0 ? 1 : 0);
			}
			break;
		}
	case PMSG_TYPE_SHUTDOWN: {
			// shutdown
			shutdown_start((struct pipes_thread_context*)ctx);
			break;
		}
	default: {
			add_msg_to_service(ctx, type, msg);
		}
	}
}
static void mailbox_to_worker(struct pipes_worker_thread_context* ctx, struct swap_message_queue* q)
{
	struct pipes_message msg;
	for (;;)
	{
		if (pipes_smq_pop_unsafe(q, &msg) == 0)  // no msg any more in swap-queue
		{
			break;
		}	
		add_msg_to_worker(ctx, &msg);
	}
}
static void mailbox_to_worker_local(struct pipes_worker_thread_context* ctx, struct message_queue* q)
{
	struct pipes_message msg;
	for (;;)
	{
		if (pipes_mq_pop_unsafe(q, &msg) == 0)  // no msg any more in queue
			{
				break;
			}	
		add_msg_to_worker(ctx, &msg);
	}
}

static void dispatch_worker_messaage(struct pipes_worker_thread_context* ctx)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct message_queue* mq;
	struct pipes_message msg;
	struct pipes_service_context* sctx;
	struct worker_message_queue* mqWorker = ctx->worker_msg_queue;
	int mqNum = 0;
	int i;
	int msgCnt = 0;
	//printf("dispatch_worker_messaage, queueNum=%d\n", mqWorker->queue_num);  // debug
	while ((mqNum = mqWorker->queue_num) > 0)
	{
		for (i = 0; i < mqNum; ++i)  // make sure every service-msg-queue can be check
			{
				mq = pipes_worker_mq_pop(mqWorker);     // pop a service-msg-queue from worker-queue
				if(mq == NULL)   // worker-queue is empty
				{
					break;
				}
				sctx = (struct pipes_service_context*)mq->udata;
				// pop msg from mq
				if( pipes_mq_pop_unsafe(mq, &msg) )
				{	
					-- thCtx->msg_num;
					proc_service_msg(ctx, sctx, &msg); // proc msg
					++ msgCnt;
					++ sctx->msg_proc;
				}
				// try readd to worker-queue
				if(!sctx->has_exit)
				{
					pipes_worker_mq_push(mqWorker, mq);
				}
			}
		if (msgCnt >= 10000)
		{
			break;
		}
	}
}

static void init_msg_thread_tick(struct pipes_msg_thread_context* ctx)
{
	//ctx->has_send_thread_cnt = 0;
}
static void flush_send_msg(struct pipes_thread_context* th)
{
	if (th->has_send_thread_cnt < 1)   // no msg has send to other thread
		{
			return;
		}
	int i;
	struct pipes_global_context* global = th->global;
	for (i = 0; i < global->msg_thread_num; ++i)
	{
		if (th->has_send_thread_flags[i] && i != th->idx)   // has send msg to other msg-thread, notify the thread
			{	
				struct pipes_msg_thread_context* other = (struct pipes_msg_thread_context*)global->threads[i];
				pipes_thread_mutex_lock(&other->mutex);
				pipes_thread_cond_signal(&other->cond);
				pipes_thread_mutex_unlock(&other->mutex);
				//
				th->has_send_thread_flags[i] = 0;     // reset signal flag
				if(--th->has_send_thread_cnt <= 0)   // all other msg-thread has notify
				{
					break;
				}
			}
	}
}
static int is_thread_pool_closed(struct pipes_global_context* global);
static void clear_destroy_service_task(struct pipes_worker_thread_context* thread);
static int has_destroy_service_task(struct pipes_worker_thread_context* thread);
void worker_thread_tick(struct pipes_worker_thread_context* ctx)
{
	struct pipes_msg_thread_context* msgCtx = (struct pipes_msg_thread_context*)ctx;
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_global_context* global = thCtx->global;
	// check shutdown
	if(thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)  // in shutdown proc
	{
		uint32_t delayMs = 100;
		if (thCtx->shutdown_state == SHUTDOWN_STATE_PROC)
		{
			ATOM_ADD(&global->shutdown_flag, 1);
			thCtx->shutdown_state = SHUTDOWN_STATE_DONE;      // change shutdown state
		}
		else if(ATOM_ADD(&global->shutdown_flag, 0) >= global->all_thread_num) // all msg-thread shutdown proc done
			{
				if (is_thread_pool_closed(global))
				{
					thCtx->loop = 0;      // stop thread loop
				}
			}
		struct timeval tmVal;
		pipes_time_now(&tmVal);
		pipes_thread_cond_timewait(&msgCtx->cond, &msgCtx->mutex, &tmVal, delayMs);
		return;
	}
	// init msg_thread tick-runtime
	init_msg_thread_tick(msgCtx);
	// dispatch msg
	dispatch_worker_messaage(ctx);
	// clear destroy task queue
	clear_destroy_service_task(ctx);
	// flush send msg
	flush_send_msg(thCtx);
	// proc msg in mailboxes
	int i, j;
	struct swap_message_queue* smq;
	// check normal swap-queue
	int noMsg = 1;
	// check msg from timer-thread
	smq = msgCtx->mailboxes[global->idx_timer];
	if (pipes_smq_swap_by_read(smq) > 0)    
	{
		noMsg = 0;
		mailbox_to_worker(ctx, smq);
	}
	// check msg from net-thread
	smq = msgCtx->mailboxes[global->idx_net];
	if (pipes_smq_swap_by_read(smq) > 0)    
	{
		noMsg = 0;
		mailbox_to_worker(ctx, smq);
	}
	// check msg in mailboxes
	for (i = 0; i < global->worker_thread_num; ++i)
	{
		j = ctx->cur_mailbox;
		ctx->cur_mailbox = (ctx->cur_mailbox + 1) % global->worker_thread_num;
		if (j != thCtx->idx)  // mailboxes of other-thread
		{
			smq = msgCtx->mailboxes[j];
			if (pipes_smq_swap_by_read(smq) > 0)  // has msg from other-thread
			{
				noMsg = 0;
				mailbox_to_worker(ctx, smq);
			}
		}
		else  // mailbox of cur-thread
		{
			struct message_queue* mq = pipes_smq_cur_readqueue(msgCtx->mailboxes[j]);
			if (pipes_mq_size_unsafe(mq) > 0)  // has msg from cur-thread
			{
				noMsg = 0;
				mailbox_to_worker_local(ctx, mq);
			}
		}
	}
	if (noMsg)  // no msg gained, check worker-queue
		{
			noMsg = ctx->worker_msg_queue->queue_num < 1 ? 1 : 0;
			//printf("workerMQNum = %d\n", ctx->worker_msg_queue->queue_num);  // debug
		}
	else
	{
		// flush send msg
		flush_send_msg(thCtx);
	}
	if (noMsg)   // no msg, check all mailboxes with lock
		{
			pipes_thread_mutex_lock(&msgCtx->mutex);
			//
			for(i = 0 ; i < global->all_thread_num ; ++i)
			{
				j = ctx->cur_mailbox;
				ctx->cur_mailbox = (ctx->cur_mailbox + 1) % global->all_thread_num;
				if (j != thCtx->idx) 
				{
					smq = msgCtx->mailboxes[j];
					if (pipes_smq_swap_by_read(smq) > 0)  // has msg in mailbox of other-threads
					{
						noMsg = 0;
						// pop msg to service-context
						mailbox_to_worker(ctx, smq);
						// flush send msg
						flush_send_msg(thCtx);
						break;
					}
				}
			}
			//
			if(noMsg || !has_destroy_service_task(ctx))  // still not found msg after strict check, cond_wait
			{
				//pipes_thread_cond_wait(&msgCtx->cond, &msgCtx->mutex);
				struct timeval tmVal;
				pipes_time_now(&tmVal);
				pipes_thread_cond_timewait(&msgCtx->cond, &msgCtx->mutex, &tmVal, WORKER_WAIT_MS);
			}
			pipes_thread_mutex_unlock(&msgCtx->mutex);
		}
	//printf("worker thread %d tick\n", thCtx->idx);  // debug
}

void net_thread_tick(struct pipes_net_thread_context* ctx)
{
	//int ret = pipes_net_tick(ctx->net_ctx);
	//printf("net thread tick: %d\n", ret);
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_global_context* global = thCtx->global;
	// check shutdown
	if(thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)  // in shutdown proc
	{
		uint32_t delayMs = 100;
		if (thCtx->shutdown_state == SHUTDOWN_STATE_PROC)
		{
			ATOM_ADD(&global->shutdown_flag, 1);
			thCtx->shutdown_state = SHUTDOWN_STATE_DONE;        // change shutdown state
		}
		else if (ATOM_ADD(&global->shutdown_flag, 0) >= global->all_thread_num) // all thread shutdown proc done
			{
				if (is_thread_pool_closed(global))
				{
					thCtx->loop = 0;        // stop thread loop
				}
			}
		struct timeval tmVal;
		pipes_time_now(&tmVal);
		pipes_thread_cond_timewait(&ctx->cond, &ctx->mutex, &tmVal, delayMs);
		return;
	}
	// tick netloop
	if(pipes_net_tick(ctx) > 0)  //has proc event
	{
		// flush send msg
		flush_send_msg(thCtx);
	}
}


// timer-thread 
static struct timer_task_wrap* alloc_timer_task_wrap(struct pipes_timer_thread_context* ctx);
static void free_timer_task_wrap(struct timer_task_wrap* wrap, struct pipes_timer_thread_context* ctx);
static void mailbox_to_timer(struct pipes_timer_thread_context* ctx, int srcThread, uint64_t tmNow, struct swap_message_queue*q);

void timer_thread_tick(struct pipes_timer_thread_context*ctx)
{
	struct pipes_msg_thread_context* msgCtx = (struct pipes_msg_thread_context*)ctx;
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_global_context* global = thCtx->global;
	// check shutdown
	if(thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)  // in shutdown proc
		{
			uint32_t delayMs = 100;
			if (thCtx->shutdown_state == SHUTDOWN_STATE_PROC)
			{
				ATOM_ADD(&global->shutdown_flag, 1);
				thCtx->shutdown_state = SHUTDOWN_STATE_DONE;       // change shutdown state
			}
			else if (ATOM_ADD(&global->shutdown_flag, 0) >= global->all_thread_num) // all thread shutdown proc done
				{
					if (is_thread_pool_closed(global))
					{
						thCtx->loop = 0;       // stop thread loop
					}
				}
			struct timeval tmVal;
			pipes_time_now(&tmVal);
			pipes_thread_cond_timewait(&msgCtx->cond, &msgCtx->mutex, &tmVal, delayMs);
			return;
		}
	// init msg_thread tick-runtime
	init_msg_thread_tick(msgCtx);
	//
	struct swap_message_queue* smq;
	int i,j;
	//
	struct timeval tmVal;
	pipes_time_now(&tmVal);
	uint64_t tmNowMs = pipes_time_toms(&tmVal);
	// check mailboxes
	int noMsg = 1;
	for (i = 0; i < global->all_thread_num; ++i)   // check mailbox
	{
		j = ctx->cur_mailbox;
		ctx->cur_mailbox = (ctx->cur_mailbox + 1) % global->all_thread_num;
		if (j == thCtx->idx)   // self
		{
			continue;
		}
		smq = msgCtx->mailboxes[j];
		if (pipes_smq_swap_by_read(smq) > 0)  // has msg from this worker-thread
		{
			noMsg = 0;
			mailbox_to_timer(ctx, j, tmNowMs, smq);  // add task to timer
		}
	}
	if (noMsg)   // no new task add, strict check
	{
		pipes_thread_mutex_lock(&msgCtx->mutex);
		for (i = 0; i < global->all_thread_num; ++i)   // check mailbox of all worker-threads
			{
				j = ctx->cur_mailbox;
				ctx->cur_mailbox = (ctx->cur_mailbox + 1) % global->all_thread_num;
				if (j == thCtx->idx)   // self
				{
					continue;
				}
				smq = msgCtx->mailboxes[j];
				if (pipes_smq_swap_by_read(smq) > 0)  // has msg from this worker-thread
					{
						noMsg = 0;
						mailbox_to_timer(ctx, j, tmNowMs, smq);   // add task to timer
						break;
					}
			}
		if (noMsg)  // still has no new task, cond_timewait
		{
			uint64_t tmNextDelay = tmwheel_advance_clock(tmNowMs, ctx->timer);
			if (tmNextDelay <= 0)   // no more task in timer
			{	
				tmNextDelay = TIMER_WAIT_MS;
			}
			if (tmNextDelay > 5)  //delay too short, no cond_wait
			{
				// flush send msg
				flush_send_msg(thCtx);
				//
				pipes_thread_cond_timewait(&msgCtx->cond, &msgCtx->mutex, &tmVal, (uint32_t)tmNextDelay);
				// get now time
				pipes_time_now(&tmVal);
				tmNowMs = pipes_time_toms(&tmVal);
				// init msg_thread tick-runtime
				init_msg_thread_tick(msgCtx);
				
				//printf("timer condwait, %lu\n", tmNowMs);
			}
		}
		pipes_thread_mutex_unlock(&msgCtx->mutex);
	}
	// advanced timer
	tmwheel_advance_clock(tmNowMs, ctx->timer);
	
	// flush send msg
	flush_send_msg(thCtx);
}

static void mailbox_to_timer(struct pipes_timer_thread_context* ctx, int srcThread, uint64_t tmNow, struct swap_message_queue*q)
{
	struct pipes_msg_thread_context* msgCtx = (struct pipes_msg_thread_context*)ctx;
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_message msg;
	for (;;)
	{
		if (pipes_smq_pop_unsafe(q, &msg) == 0) // no more msg
			{
				break;
			}
		if (thCtx->shutdown_state >= SHUTDOWN_STATE_PROC)
		{
			destroy_pipes_msg(&msg);
			continue;
		}
		int type = pipes_api_dec_msgtype(msg.size);
		if (type == PMSG_TYPE_SHUTDOWN)  // require shutdown
		{
			shutdown_start((struct pipes_thread_context*)ctx);
			continue;
		}
		// add to timer
		struct timer_task_wrap* wrap = alloc_timer_task_wrap(ctx);
		//
		wrap->ctx = ctx;
		wrap->src_thread = srcThread;
		wrap->src_id = msg.source;
		wrap->session = msg.session;
		wrap->dur = (msg.dest << 24) >> 24;
		wrap->expiration = tmNow + wrap->dur;
		int lastCnt = msg.dest >> 40;
		wrap->last_cnt = lastCnt <= 0 ? 1 : lastCnt;
		// debug
		//printf(">>>>>>>> timer add, ss=%d, poolIdx=%d, delay=%ld, now=%ld, exp=%ld\n", 
			//wrap->session, wrap->idx, wrap->dur, tmNow, wrap->expiration);
		//
		tmwheel_add_task(wrap->expiration, wrap, ctx->timer, tmNow);
	}
}
void on_expire_callback(void* udata, uint64_t tmNow)
{
	struct timer_task_wrap* task = (struct timer_task_wrap*)udata;
	struct pipes_timer_thread_context* ctx = task->ctx;
	struct pipes_thread_context* th = (struct pipes_thread_context*)ctx;
	// debug
	//printf("======== debug timer_cb, ss=%d, poolIdx=%d, now=%ld, taskCnt = %d\n", 
		//task->session, task->idx, tmNow, task->last_cnt);
	//send timeout msg
	int toTh = pipes_handle_get_thread(task->src_id);
	if (toTh == th->global->idx_net)  // task of net-thread
	{
		pipes_api_net_timeout(ctx->sys_service, task->session);
	}
	else
	{
		pipes_api_send(ctx->sys_service, task->src_id, PMSG_TYPE_TIMEOUT, -task->session, NULL, task->last_cnt);
	}
	if (--task->last_cnt < 1)  // free task wrap
		{
			free_timer_task_wrap(task, ctx); 
		}
	else  // re-add
	{
		task->expiration = tmNow + task->dur;
		tmwheel_add_task(task->expiration, task, ctx->timer, tmNow);
	}
}
static struct timer_task_wrap* alloc_timer_task_wrap(struct pipes_timer_thread_context* ctx)
{
	if (ctx->free_num < 1)
	{
		// pool is full, resize
		size_t oldCap = ctx->pool_cap;
		struct timer_task_wrap* oldPool = ctx->task_pool;
		int* oldFreePool = ctx->free_pool;
		// new pool
		ctx->pool_cap *= 2;
		size_t sz = sizeof(struct timer_task_wrap) * ctx->pool_cap;
		ctx->task_pool = pipes_malloc(sz);
		memcpy(ctx->task_pool, oldPool, sizeof(struct timer_task_wrap) * oldCap);
		//
		ctx->free_pool = pipes_malloc(sizeof(int) * ctx->pool_cap);
		int i;
		for (i = oldCap; i < ctx->pool_cap; ++i)
		{
			ctx->task_pool[i].idx = i;
			ctx->free_pool[i - oldCap] = i;
		}
		ctx->free_num = oldCap;	
		//
		pipes_free(oldPool);
		pipes_free(oldFreePool);
	}
	int idx = ctx->free_pool[--ctx->free_num];
	return &ctx->task_pool[idx];
	
}
static void free_timer_task_wrap(struct timer_task_wrap* wrap, struct pipes_timer_thread_context* ctx)
{
	ctx->free_pool[ctx->free_num++] = wrap->idx;
}
void init_timer_task_pool(struct pipes_timer_thread_context* ctx)
{
	// init task-pool
	ctx->pool_cap = TIMER_TASK_POOL;
	size_t sz = sizeof(struct timer_task_wrap) * ctx->pool_cap;
	ctx->task_pool = pipes_malloc(sz);
	memset(ctx->task_pool, 0, sz);
	// init free-pool
	sz = sizeof(int) * ctx->pool_cap;
	ctx->free_pool = pipes_malloc(sz);
	int i;
	for (i=0; i<ctx->pool_cap; ++i)
	{
		ctx->task_pool[i].idx = i;
		ctx->free_pool[i] = i;
	}
	ctx->free_num = ctx->pool_cap;
}

// destroy thread
static void destroy_thread(struct pipes_thread_context* ctx)
{
	//struct pipes_global_context* global = ctx->global;
	//pipes_handle_destroy_storage(global->handle_mgr);
	//global->handle_mgr = NULL;
	if(ctx->local_buf)
	{
		pipes_free(ctx->local_buf);
		ctx->local_buf = NULL;
		ctx->local_buf_size = 0;
	}
}
static void destroy_msg_thread(struct pipes_msg_thread_context* ctx)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)ctx;
	struct pipes_global_context* global = thCtx->global;
	struct pipes_message msg;
	int i, j;
	// clean msgs in mailbox
	for (i=0; i<global->msg_thread_num; ++i)
	{
		struct swap_message_queue * smq = ctx->mailboxes[i];
		if (i != thCtx->idx)  // not cur thread
		{
			for (j=0;;)
			{
				if (j == 1)
				{
					if (pipes_smq_swap_by_read(smq) <= 0)  // has msg from other-thread
						{
							break;
						}
					j = 2;
				}
				if (pipes_smq_pop_unsafe(smq, &msg) == 0)  // no msg any more in swap-queue
					{
						if (j == 0)  // swap smq
							{
								j = 1;
								continue;
							}
						break;
					}	
				destroy_pipes_msg(&msg);
			}
		}
		else  // cur thread
		{
			struct message_queue* mq = pipes_smq_cur_readqueue(smq);
			if (pipes_mq_size_unsafe(mq) > 0)  // has msg from cur-thread
				{
					for (;;)
					{
						if (pipes_mq_pop_unsafe(mq, &msg) == 0)  // no msg any more in queue
							{
								break;
							}
						destroy_pipes_msg(&msg);
					}
				}	
		}
	}
	// 
	pipes_free(ctx->mailboxes);
	pipes_thread_cond_destroy(&ctx->cond);
	pipes_thread_mutex_destroy(&ctx->mutex);
	
}
static void on_service_handle_retire(void* udata, void* ctx)
{
	destroy_service(ctx);
}
void destroy_worker_thread(struct pipes_worker_thread_context* ctx)
{
	// clear destroy task queue
	clear_destroy_service_task(ctx);
	//
	destroy_thread((struct pipes_thread_context*)ctx);
	destroy_msg_thread((struct pipes_msg_thread_context*)ctx);
	// clean msg in local-services
	pipes_handle_destroy_storage_unsafe(ctx->handle_mgr, NULL, on_service_handle_retire);
	ctx->handle_mgr = NULL;
	// destroy sys-service
	pipes_free(ctx->sys_service);
	ctx->sys_service = NULL;
	//
	pipes_free(ctx->worker_msg_queue);
	ctx->worker_msg_queue = NULL;
}
void destroy_timer_thread(struct pipes_timer_thread_context* ctx)
{
	destroy_thread((struct pipes_thread_context*)ctx);
	destroy_msg_thread((struct pipes_msg_thread_context*)ctx);
	// destroy timer
	tmwheel_destroy_timer(ctx->timer);
	ctx->timer = NULL;
	//
	pipes_free(ctx->task_pool);
	ctx->task_pool = NULL;
	pipes_free(ctx->free_pool);
	ctx->free_pool = NULL;
}
void destroy_net_thread(struct pipes_net_thread_context* ctx)
{
	destroy_thread((struct pipes_thread_context*)ctx);
	pipes_net_destroy(ctx->net_ctx);
}
static void on_globalservice_handle_retire(void* udata, void* ctx)
{
	destroy_service(ctx);
}
void destroy_global(struct pipes_global_context* global)
{
	pipes_handle_destroy_storage(global->handle_mgr, NULL, on_globalservice_handle_retire);
	global->handle_mgr = NULL;
	pipes_free(global->threads);
}

//
static int is_thread_pool_closed(struct pipes_global_context* global)
{
	int closed = 0;
	struct pipes_thread_pool* pool = &global->th_pool;
	SPIN_LOCK(pool);
	if (pool->exec_th_num == 0 && pool->call_close)
	{
		pool->closed = 1;
	}
	closed = pool->closed;
	SPIN_UNLOCK(pool);
	return closed;
}


//
uint64_t pipes_get_context_handle(void* ctx)
{
	return ((struct pipes_service_context*)ctx)->handle;
}
void pipes_set_context_handle(void*ctx, uint64_t handle)
{
	((struct pipes_service_context*)ctx)->handle = handle;
}


// api
struct post_task_wrap
{
	struct pipes_global_context* g;
	void* param;
	fn_post_task fn;
}
;
static void post_task_thread(void* arg)
{
	struct post_task_wrap* wrap = (struct post_task_wrap*)arg;
	// cb
	wrap->fn(wrap->param, wrap->g);
	//
	struct pipes_thread_pool* pool = &wrap->g->th_pool;
	pipes_free(wrap);
	SPIN_LOCK(pool);
	if (--pool->exec_th_num == 0 && pool->call_close) // no more task & has called close
	{
		pool->closed = 1;	
	}
	SPIN_UNLOCK(pool);
}
int pipes_api_post_task(struct pipes_global_context* g, void* param, fn_post_task fn)
{
	int ret = -1;
	struct pipes_thread_pool* pool = &g->th_pool;
	SPIN_LOCK(pool);
	do
	{
		if (pool->call_close)  // has called close
		{
			break;
		}
		// post task
		struct post_task_wrap* wrap = pipes_malloc(sizeof(struct post_task_wrap));
		wrap->g = g;
		wrap->param = param;
		wrap->fn = fn;
		THREAD_FD fd;
		pipes_thread_start(&fd, post_task_thread, wrap);
		pool->exec_th_num++;
		ret = 0;
	} while (0);
	SPIN_UNLOCK(pool);
	return ret;
}
int pipes_api_close_thread_pool(struct pipes_global_context* g)
{
	struct pipes_thread_pool* pool = &g->th_pool;
	SPIN_LOCK(pool);
	pool->call_close = 1;
	SPIN_UNLOCK(pool);
	return 1;
}
static void _mark_thread_send(struct pipes_global_context*global, int curThread, int toThread)
{
	struct pipes_thread_context* ctxCur = (struct pipes_thread_context*)global->threads[curThread];
	if (ctxCur->has_send_thread_flags[toThread] == 0)
	{
		ctxCur->has_send_thread_flags[toThread] = 1;
		ctxCur->has_send_thread_cnt++;
	}
}
static int _send(struct pipes_global_context*global, int curThread, int toThread, struct pipes_message* msg)
{
	assert(toThread > -1 && toThread < global->msg_thread_num);
	struct pipes_msg_thread_context* msgCtxTo = (struct pipes_msg_thread_context*)global->threads[toThread];
	if (curThread != toThread)
	{	
		struct swap_message_queue* smq = msgCtxTo->mailboxes[curThread];
		pipes_smq_push(smq, msg);
		//printf("_send, thSelf=%d, thTo=%d, smq=%p\n", curThread, toThread, smq);   // debug
	}
	else
	{
		struct message_queue* mq = pipes_smq_cur_readqueue(msgCtxTo->mailboxes[curThread]);
		pipes_mq_push_unsafe(mq, msg);
	}
	_mark_thread_send(global, curThread, toThread);
	return 1;
}
static int _send_priority(struct pipes_global_context*global, int curThread, int toThread, struct pipes_message* msg)
{
	assert(toThread > -1 && toThread < global->msg_thread_num);
	struct pipes_msg_thread_context* msgCtxTo = (struct pipes_msg_thread_context*)global->threads[toThread];
	if (curThread != toThread)
	{
		pipes_smq_inserthead(msgCtxTo->mailboxes[curThread], msg);
	}
	else
	{
		struct message_queue* mq = pipes_smq_cur_readqueue(msgCtxTo->mailboxes[curThread]);
		pipes_mq_inserthead_unsafe(mq, msg);
	}
	_mark_thread_send(global, curThread, toThread);
	return 1;
}
static void _fill_send_msg(
	struct pipes_message* m,
	uint64_t from,
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size)
{
	m->source = from;
	m ->dest = to;
	m->session = session;
	if (data)
	{
		m->data = data; 
	}
	else
	{
		m->data = NULL;
	}
	m->size = pipes_api_enc_msgtype(type, size);
}

int pipes_api_create_service(
	struct pipes_service_context*caller,
	int toThread,
	int session,
	void* adapter, 
	const char * name, 
	uint64_t* handle)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	assert(toThread > -1  && toThread < global->worker_thread_num);
	size_t sz = sizeof(struct pipes_service_context);
	struct pipes_service_context* service = pipes_malloc(sz);
	memset(service, 0, sz);
	// register handle
	char* nameAdded = NULL;
	*handle = pipes_handle_register(service, toThread, global->handle_mgr);
	if (name)   // specify name, try to add
	{
		nameAdded = (char*)pipes_handle_namehandle(*handle, name, global->handle_mgr);
		if (nameAdded == NULL)   // name exist, failed
		{
			pipes_handle_retire(*handle, global->handle_mgr);
			pipes_free(service);
			return 0;
		}
	}
	// send to dst thread
	sz = sizeof(struct create_service_tmp);
	struct create_service_tmp* createData = pipes_malloc(sz);
	memset(createData, 0, sz);
	createData->adapter = adapter;
	createData->name = nameAdded;
	//
	//pipes_api_send_priority(caller, *handle, PMSG_TYPE_INIT_SERVICE, session, createData, 0);
	pipes_api_send(caller, *handle, PMSG_TYPE_INIT_SERVICE, session, createData, 0);
	return 1;
}

static int has_destroy_service_task(struct pipes_worker_thread_context* thread)
{
	return thread->destroy_head != NULL;
}
static void clear_destroy_service_task(struct pipes_worker_thread_context* thread)
{
	while (thread->destroy_head)
	{
		destroy_service(thread->destroy_head);
		thread->destroy_head = thread->destroy_head->next;
	}
	thread->destroy_tail = NULL;
}
static void add_destroy_service_task(struct pipes_worker_thread_context* thread, struct pipes_service_context* service)
{
	if (thread->destroy_tail)
	{
		thread->destroy_tail->next = service;
		thread->destroy_tail = service;
	}
	else
	{
		thread->destroy_head = service;
		thread->destroy_tail = service;
	}
}
int pipes_api_exit_service(struct pipes_service_context*caller)
{
	int succ = 0;
	if (!caller->has_exit)
	{
		caller->has_exit = 1;
		add_destroy_service_task((struct pipes_worker_thread_context*)caller->thread, caller);
		succ = 1;
		//
		-- caller->thread->service_num; // decrease service-num
	}
	return succ;
}
int pipes_api_redirect(
	struct pipes_service_context*caller,
	uint64_t to, 
	struct pipes_message* msg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	int toThread = pipes_handle_get_thread(to);
	msg->dest = to;
	_send(thCtx->global, thCtx->idx, toThread, msg);
	return 1;
}
int pipes_api_send_sys(
	struct pipes_global_context* g,
	int curThread,
	uint64_t from,
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size)
{
	struct pipes_message msg;
	_fill_send_msg(&msg, from, to, type, session, data, size);
	int toThread = pipes_handle_get_thread(to);
	return _send(g, curThread, toThread, &msg);
}
int pipes_api_send(
	struct pipes_service_context*caller,  
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_message msg;
	_fill_send_msg(&msg, caller->handle, to, type, session, data, size);
	//
	int toThread = pipes_handle_get_thread(to);
	return _send(thCtx->global, thCtx->idx, toThread, &msg);	
}
int pipes_api_send_priority(
	struct pipes_service_context*caller,  
	uint64_t to,
	int type,
	int session,
	void* data,
	uint32_t size)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_message msg;
	_fill_send_msg(&msg, caller->handle, to, type, session, data, size);
	//
	int toThread = pipes_handle_get_thread(to);
	return _send_priority(thCtx->global, thCtx->idx, toThread, &msg);
}
int pipes_api_timeout(struct pipes_service_context* caller, uint32_t delay, int session, uint64_t lastCnt)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_message msg;
	_fill_send_msg(&msg, caller->handle, (lastCnt<<40)|delay, PMSG_TYPE_TIMEOUT, session, NULL, 0);
	return _send(thCtx->global, thCtx->idx, thCtx->global->idx_timer, &msg);
}



// api for net
static void send_to_net(
	struct pipes_thread_context* th,
	struct pipes_net_context* net, unsigned char cmd, 
	uint32_t id, int session, uint64_t source, void* data, size_t sz)
{
	size_t szBuf = 1 + 4 + 4 + 8; //cmd(1), id(4), session(4), source(8)
	if (data)
	{
		szBuf += sz;
	}
	void* buf = th->tmp_buf; 
	((unsigned char*)buf)[0] = cmd;
	memcpy(buf + 1, &id, 4);
	memcpy(buf + 5, &session, 4);
	memcpy(buf + 9, &source, 8);
	if (data)
	{
		memcpy(buf + 17, data, sz);
	}
	pipes_net_send_msg(net, buf, szBuf);
}
int pipes_api_tcp_listen(struct pipes_service_context* caller, int session, struct pipes_tcp_server_cfg* cfg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	//
	pipes_net_tcp_listen(global->net_ctx, session, caller->handle, cfg);
	return 1;
}
int pipes_api_tcp_connect(struct pipes_service_context* caller, int session, struct pipes_tcp_client_cfg * cfg)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	//
	pipes_net_tcp_connect(global->net_ctx, session, caller->handle, cfg);
	return 1;
}
int pipes_api_net_timeout(struct pipes_service_context* caller, int session)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	//
	pipes_net_connect_timeout(global->net_ctx, session);
	return 1;
}
int pipes_api_tcp_session_start(struct pipes_service_context* caller, uint32_t id, uint64_t source)
{
	int ret = pipes_net_start_session(caller->thread->global->net_ctx, id, source);
	return ret;
	/*
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_message msg;
	//
	struct net_tcp_session_start req;
	req.source = source;
	//
	size_t sz = 0;
	void* data = wrap_net_msg(NET_CMD_TCP_SESSION_START, id, &req, sizeof(req), &sz);
	_fill_send_msg(&msg, caller->handle, 0, PMSG_TYPE_NET, 0, data, sz);
	int ret = _send(thCtx->global, thCtx->idx, thCtx->global->idx_net, &msg);
	pipes_net_notify(thCtx->global->net_ctx);  // notify net thread
	return ret;
	*/
}
int pipes_api_tcp_close(struct pipes_service_context* caller, uint32_t id)
{
	int ret = pipes_net_close_tcp(caller->thread->global->net_ctx, id);
	return ret;
	/*
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_message msg;
	//
	size_t sz = 0;
	void* data = wrap_net_msg(NET_CMD_TCP_CLOSE, id, NULL, 0, &sz);
	_fill_send_msg(&msg, caller->handle, 0, PMSG_TYPE_NET, 0, data, sz);
	int ret = _send(thCtx->global, thCtx->idx, thCtx->global->idx_net, &msg);
//	pipes_net_notify(thCtx->global->net_ctx);   // notify net thread
	return ret;
	*/
}

//
void pipes_api_shutdown(struct pipes_service_context* caller)
{
	struct pipes_thread_context* thCtx = (struct pipes_thread_context*)caller->thread;
	struct pipes_global_context* global = thCtx->global;
	// send shutdown msg to all msg thread
	int i;
	//shutdown task-pool
	pipes_api_close_thread_pool(global);
	//shutdown net
	pipes_net_shutdown(caller->thread->global->net_ctx);
	// shutdown workers & timer
	for (i=0; i<global->msg_thread_num; ++i)
	{
		struct pipes_msg_thread_context* dstMsgTh = (struct pipes_msg_thread_context*)global->threads[i];
		uint64_t dstSysHandle = pipes_handle_reserve_handle(global->harbor, dstMsgTh->thread_ctx.idx);
		pipes_api_send_priority(caller, dstSysHandle, PMSG_TYPE_SHUTDOWN, 0, NULL, 0);
	}
}
void* pipes_api_get_adapter(struct pipes_service_context* ctx)
{
	return ctx->adapter;
}

uint64_t pipes_api_handle(struct pipes_service_context* ctx)
{
	return ctx->handle;
}
int pipes_api_corethreadnum()
{
	return CORE_THREAD_NUM;
}
//
uint32_t pipes_api_enc_msgtype(int msgType, uint32_t msgSize)
{
	uint32_t id = 0;
	
	return ((id | msgType) << 24) | ((msgSize << 8) >> 8);
}
int pipes_api_dec_msgtype(uint32_t msgSize)
{
	return msgSize >> 24;
}
uint32_t pipes_api_msg_sizeraw(uint32_t msgSize)
{
	return (msgSize << 8) >> 8;
}

//
uint64_t pipes_api_gethandle_byname(struct pipes_service_context* caller, const char* name)
{
	return pipes_handle_findname(name, caller->thread->global->handle_mgr);
}
struct pipes_thread_context* pipes_api_getthread(struct pipes_service_context* service)
{
	return (struct pipes_thread_context*)service->thread;
}
// thread-local buf
void* pipes_api_threadlocal_malloc(struct pipes_thread_context* thread, size_t szRequire, size_t* szActual)
{
	if (thread->local_buf == NULL || szRequire > thread->local_buf_size)
	{
		if (thread->local_buf)
		{
			pipes_free(thread->local_buf);
		}
		thread->local_buf = pipes_malloc(szRequire);
		thread->local_buf_size = szRequire;
		*szActual = szRequire;
	}
	else
	{
		*szActual = thread->local_buf_size;
	}
	return thread->local_buf;
}
void* pipes_api_threadlocal_realloc(
	struct pipes_thread_context* thread,
	void* ptr,
	size_t szUsed,
	size_t szNewPrefer, 
	size_t szNewMin, 
	size_t* szNewActual)
{
	assert(ptr && ptr == thread->local_buf);
	int incrPrefer = szNewPrefer - thread->local_buf_size;
	int incrMin = szNewMin - thread->local_buf_size;
	if (incrPrefer <= 0)
	{
		*szNewActual = szNewPrefer;
	}
	else if (incrMin <= 0)
	{
		*szNewActual = szNewMin;
	}
	else  // need realloc
	{
		size_t incr = incrPrefer <= THREAD_LOCAL_MEM_INCR_STEP ? incrPrefer : incrMin;
		thread->local_buf_size += incr;
		void* newBuf = pipes_malloc(thread->local_buf_size);
		memcpy(newBuf, thread->local_buf, szUsed);
		pipes_free(thread->local_buf);
		thread->local_buf = newBuf;
	}
	return thread->local_buf;
}
void pipes_api_threadlocal_free(struct pipes_thread_context* thread, void* ptr)
{
	
}




