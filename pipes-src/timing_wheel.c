
#include "timing_wheel.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TW_NODE_IDX_ROOT -1
#define TW_NODE_IDX_NULL -2


struct tw_task
{
	int64_t expiration;
	void* udata;
	int prev;
	int next;
	int pool_idx;
};
struct tw_slot
{
	int64_t expiration;
	struct tw_task* root;
};
struct tw_wheel
{
	uint32_t wheel_size;
	uint64_t tick_dur;
	uint64_t interval;
	uint64_t cur_time;
	struct tw_slot** slots;
	struct tw_wheel* up_wheel;
};

struct tw_timer
{
	fn_tmwheel_callback fn_callback;
	fn_tmwheel_malloc fn_malloc;
	fn_tmwheel_free fn_free;
	struct tw_wheel* root;
	struct minheap_queue* delay_queue;
	//
	struct tw_task* task_pool;
	int pool_cap;
	int* free_pool;
	int free_num;
	//
	uint32_t wheel_depth;
	//
	uint64_t cur_total_task;
};

static void dbg_dump_task_pool(struct tw_timer* timer)
{
	int i;
	for (i=0; i<timer->pool_cap; ++i)
	{
		if (timer->task_pool[i].pool_idx < 0)
		{
			int i = 1;
		}
	}
}

static void add_slot_to_queue(struct tw_slot* slot, struct tw_timer* timer)
{
	minheap_add_node(slot, timer->delay_queue);
}

static struct tw_task* alloc_task_wrap(struct tw_timer* timer)
{	
	if (timer->free_num <= 0)   // pool is full, no free wrap, resize
	{
		int oldCap = timer->pool_cap;
		struct tw_task* oldPool = timer->task_pool;
		int* oldFreePool = timer->free_pool;
		//
		timer->pool_cap *= 2;
		size_t sz = sizeof(struct tw_task) * timer->pool_cap;
		timer->task_pool = timer->fn_malloc(sz);
		sz = sizeof(int) * timer->pool_cap;
		timer->free_pool = timer->fn_malloc(sz);
		memcpy(timer->task_pool, oldPool, sizeof(struct tw_task)*oldCap);
		int i;
		for (i = oldCap; i < timer->pool_cap; ++i)
		{
			timer->task_pool[i].pool_idx = i;
			timer->free_pool[i - oldCap] = i;
		}
		timer->free_num = oldCap;
		//
		timer->fn_free(oldPool);
		timer->fn_free(oldFreePool);
		//
	}
	struct tw_task* task = &timer->task_pool[timer->free_pool[--timer->free_num]];
	task->next = TW_NODE_IDX_NULL;
	task->prev = TW_NODE_IDX_NULL;
	return task;
}

static void free_task_wrap(struct tw_task* task, struct tw_timer* timer)
{
	assert(timer->free_num < timer->pool_cap);
	timer->free_pool[timer->free_num++] = task->pool_idx;
}

static int add_task_to_wheel(struct tw_task* task, struct tw_wheel* wheel, struct tw_timer* timer);
static void add_task(struct tw_task* task, struct tw_timer* timer, uint64_t tmNow)
{
	int ret = add_task_to_wheel(task, timer->root, timer);
	if (!ret)   // expired, call back & free task-wrap
		{
			//expired, callback
			timer->fn_callback(task->udata, tmNow);
			//free task-wrap
			free_task_wrap(task, timer);
		}
}

static struct tw_task* trans_idx_to_node(int idx, struct tw_task* root, struct tw_timer* timer)
{
	if (idx == TW_NODE_IDX_ROOT)
	{
		return root;
	}
	if (idx >= 0)
	{
		return &timer->task_pool[idx];
	}
	return NULL;
}
static int trans_node_to_idx(struct tw_task* node, struct tw_task* root)
{
	if (node == root)
	{
		return TW_NODE_IDX_ROOT;
	}
	if (node == NULL)
	{
		return TW_NODE_IDX_NULL;
	}
	return node->pool_idx;
}

static int set_slot_expiration(int64_t expiration, struct tw_slot* slot, struct tw_timer* timer)
{
	if (expiration != slot->expiration)
	{
		slot->expiration = expiration;
		return 1;
	}
	return 0;
}

static void add_task_to_slot(struct tw_task* task, struct tw_slot* slot, struct tw_timer* timer)
{
	struct tw_task* root = slot->root;
	struct tw_task* tail = trans_idx_to_node(root->prev, root, timer); 
	task->next = trans_node_to_idx(root, root);
	task->prev = trans_node_to_idx(tail, root);
	tail->next = trans_node_to_idx(task, root);
	root->prev = tail->next;
	
	++timer->cur_total_task;
	/*
	 *		val tail = root.prev
            timerTaskEntry.next = root
            timerTaskEntry.prev = tail
            tail.next = timerTaskEntry
            root.prev = timerTaskEntry
	 **/
}
static void remove_task_from_slot(struct tw_task* task, struct tw_slot* slot, struct tw_timer* timer)
{
	struct tw_task* root = slot->root;
	trans_idx_to_node(task->next, root, timer)->prev = task->prev;
	trans_idx_to_node(task->prev, root, timer)->next = task->next;
	task->next = TW_NODE_IDX_NULL;
	task->prev = TW_NODE_IDX_NULL;
	
	--timer->cur_total_task;
	/*
	      timerTaskEntry.next.prev = timerTaskEntry.prev
          timerTaskEntry.prev.next = timerTaskEntry.next
          timerTaskEntry.next = null
          timerTaskEntry.prev = null
	 */
}

static void flush_slot(struct tw_slot* slot, struct tw_timer* timer, uint64_t tmNow)
{
	struct tw_task* root = slot->root;
	struct tw_task* head = trans_idx_to_node(root->next, root, timer);
	// reset expiration
	set_slot_expiration(-1, slot, timer);
	//
	while (head != root)
	{
		remove_task_from_slot(head, slot, timer);
		// re-add
		add_task(head, timer, tmNow);
		//
		head = trans_idx_to_node(root->next, root, timer);
	}
	/*
	 *var head = root.next
      while (head ne root) {
        remove(head)
        f(head)
        head = root.next
      }
      expiration.set(-1L)
	 **/
}
 
static struct tw_slot* create_slot(struct tw_timer* timer)
{
	struct tw_slot* slot = NULL;
	size_t sz = sizeof(struct tw_slot);
	slot = timer->fn_malloc(sz);
	
	// init root node
	struct tw_task* root = timer->fn_malloc(sizeof(struct tw_task));
	slot->root = root;
	root->pool_idx = TW_NODE_IDX_ROOT;
	root->next = TW_NODE_IDX_ROOT;
	root->prev = TW_NODE_IDX_ROOT;
	root->expiration = -1;
	
	set_slot_expiration(-1, slot, timer);
	
	return slot;
};

static struct tw_wheel* create_wheel(uint64_t tickDur, uint32_t wheelSize, uint64_t startTime, struct tw_timer* timer)
{	
	struct tw_wheel* wheel = NULL;
	
	size_t sz = sizeof(struct tw_wheel);
	wheel = timer->fn_malloc(sz);
	memset(wheel, 0, sz);
	
	wheel->tick_dur = tickDur;
	wheel->wheel_size = wheelSize;
	wheel->interval = tickDur * wheelSize;
	wheel->cur_time = startTime - startTime % tickDur;
	
	//create slots
	sz = sizeof(struct tw_slot*) * wheel->wheel_size;
	wheel->slots = timer->fn_malloc(sz);
	int i;
	for (i=0; i<wheel->wheel_size; ++i)
	{
		wheel->slots[i] = create_slot(timer);
	}
	
	wheel->up_wheel = NULL;
	
	++timer->wheel_depth;
	
	return wheel;
}

static int add_task_to_wheel(struct tw_task* task, struct tw_wheel* wheel, struct tw_timer* timer)
{
	if (task->expiration < wheel->cur_time + wheel->tick_dur)
	{
		//already expired
		return 0;
	}
	else if (task->expiration < wheel->cur_time + wheel->interval)
	{
		// add to cur wheel
		uint64_t idSlot = (task->expiration / wheel->tick_dur);
		struct tw_slot* slot = wheel->slots[idSlot % wheel->wheel_size];
		add_task_to_slot(task, slot, timer);
		if (set_slot_expiration(idSlot * wheel->tick_dur, slot, timer))
		{
			add_slot_to_queue(slot, timer);
		}
		return 1;
	}
	else
	{
		// add to up wheel
		if(wheel->up_wheel == NULL) 
		{
			//create up wheel
			wheel->up_wheel = create_wheel(wheel->interval, wheel->wheel_size, wheel->cur_time, timer);
		}
		return add_task_to_wheel(task, wheel->up_wheel, timer);
	}
}

static void advance_clock(uint64_t time, struct tw_wheel* wheel, struct tw_timer* timer)
{
	if (time >= wheel->cur_time + wheel->tick_dur) // can tick, update time
	{
		wheel->cur_time = time - (time % wheel->tick_dur);
		//advance up-wheel
		if(wheel->up_wheel != NULL)
		{
			advance_clock(wheel->cur_time, wheel->up_wheel, timer);
		}
	}
}

static int slot_compare(void* d1, void* d2)
{
	struct tw_slot* s1 = (struct tw_slot*)d1;
	struct tw_slot* s2 = (struct tw_slot*)d2;
	if (s1->expiration < s2->expiration)
	{
		return -1;
	}
	if (s1->expiration > s2->expiration)
	{
		return 1;
	}
	return 0;
}

//
struct tw_timer* tmwheel_create_timer(
	uint32_t tickDur,
	uint32_t wheelSize,
	uint64_t startTime,
	fn_tmwheel_callback fnCallback,
	fn_tmwheel_malloc fnMalloc,
	fn_tmwheel_free fnFree,
	uint32_t taskPoolCap,
	uint32_t delayQueueCap)
{
	assert(tickDur > 0);
	assert(wheelSize > 0);
	assert(taskPoolCap > 0);
	assert(delayQueueCap > 0);
	//
	struct tw_timer* timer = NULL;
	size_t sz = sizeof(struct tw_timer);
	timer = fnMalloc(sz);
	memset(timer, 0, sz);
	//
	timer->fn_callback = fnCallback;
	timer->fn_malloc = fnMalloc;
	timer->fn_free = fnFree;
	//
	struct tw_wheel* wheel = create_wheel(tickDur, wheelSize, startTime, timer);
	timer->root = wheel;
	timer->wheel_depth = 1;
	//
	timer->delay_queue = minheap_create_queue(delayQueueCap, slot_compare, fnMalloc, fnFree);
	// init task pool
	timer->pool_cap = taskPoolCap;
	sz = sizeof(struct tw_task) * timer->pool_cap;
	timer->task_pool = fnMalloc(sz);
	// init free pool
	sz = sizeof(int) * timer->pool_cap;
	timer->free_pool = fnMalloc(sz);
	int i;
	for (i=0; i<timer->pool_cap; ++i)
	{
		timer->task_pool[i].pool_idx = i;
		timer->free_pool[i] = i;
	}
	timer->free_num = timer->pool_cap;
	
	return timer;
};

void tmwheel_destroy_timer(struct tw_timer* timer)
{
	
}

void tmwheel_add_task(uint64_t expiration, void* udata, struct tw_timer* timer, uint64_t tmNow)
{
	struct tw_task* task = alloc_task_wrap(timer);
	task->expiration = expiration;
	task->udata = udata;
	//
	add_task(task, timer, tmNow);
}

uint64_t tmwheel_advance_clock(uint64_t timeNow, struct tw_timer* timer)
{
	struct tw_slot* slot;
	while ( (slot = (struct tw_slot*)minheap_get_min(timer->delay_queue)) != NULL )
	{
		if (slot->expiration <= timeNow)   // expired
			{
				minheap_pop_min(timer->delay_queue);    // pop from delay-queue
				advance_clock(slot->expiration, timer->root, timer);    // advance time-wheel
				flush_slot(slot, timer, timeNow);    // flush cur slot
			}
		else   // return next-slot delayed time
		{
			return slot->expiration - timeNow;
		}
	}
	// no slot in delay-queue, return 0
	return 0;
}

uint64_t tmwheel_cur_total_task(struct tw_timer* timer)
{
	return timer->cur_total_task;
}


// debug
#include <stdio.h>
void tmwheel_debug_step(struct tw_timer* timer, int arg, void* ud)
{
	if (arg == 0)  //add task done
	{
		printf("<<<<<<<<< timer add task, ss=%d\n", *((int*)ud));
	}
}


