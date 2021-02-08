
#ifndef PIPES_SERVER_H
#define PIPES_SERVER_H

#include <stdint.h>

//
#define SHUTDOWN_STATE_NO 0
#define SHUTDOWN_STATE_PROC 1
#define SHUTDOWN_STATE_DONE 2

struct pipes_service_context;
struct pipes_worker_thread_context;
struct pipes_global_context;
struct pipes_timer_thread_context;
struct pipes_net_thread_context;

uint64_t pipes_get_context_handle(void*);
void pipes_set_context_handle(void*, uint64_t);


void worker_thread_tick(struct pipes_worker_thread_context*);

void timer_thread_tick(struct pipes_timer_thread_context*);

void net_thread_tick(struct pipes_net_thread_context*);

void destroy_worker_thread(struct pipes_worker_thread_context*);

void destroy_timer_thread(struct pipes_timer_thread_context*);

void destroy_net_thread(struct pipes_net_thread_context*);

void destroy_global(struct pipes_global_context*);

void on_expire_callback(void* udata, uint64_t tmNow);

void init_timer_task_pool(struct pipes_timer_thread_context*);



#endif 
