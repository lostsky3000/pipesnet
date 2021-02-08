#ifndef TIMING_WHEEL_H
#define TIMING_WHEEL_H

#include <stdint.h>
#include <stddef.h>
#include "minheap.h"

typedef void*(*fn_tmwheel_malloc)(size_t);
typedef void(*fn_tmwheel_free)(void*);

typedef void(*fn_tmwheel_callback)(void* udata, uint64_t tmNow);


struct tw_timer;


struct tw_timer* tmwheel_create_timer(
	uint32_t tickDur, 
	uint32_t wheelSize,
	uint64_t startTime,
	fn_tmwheel_callback fnCallback,
	fn_tmwheel_malloc fnMalloc, 
	fn_tmwheel_free fnFree,
	uint32_t taskPoolCap,
	uint32_t delayQueueCap);

void tmwheel_destroy_timer(struct tw_timer*);

void tmwheel_add_task(uint64_t expiration, void* udata, struct tw_timer* timer, uint64_t tmNow);

uint64_t tmwheel_advance_clock(uint64_t timeNow, struct tw_timer* timer);

uint64_t tmwheel_cur_total_task(struct tw_timer* timer);


void tmwheel_debug_step(struct tw_timer* timer, int arg, void* ud);

#endif // !TIMING_WHEEL_H

