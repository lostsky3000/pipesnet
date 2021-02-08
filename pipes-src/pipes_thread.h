
#ifndef PIPES_THREAD_H
#define PIPES_THREAD_H

#include "pipes_macro.h"
#include "pipes_time.h"

#include <stdint.h>


#ifdef SYS_IS_LINUX
    #include <pthread.h>
    #define THREAD_FD pthread_t
    #define THREAD_MUTEX pthread_mutex_t 
    #define THREAD_COND pthread_cond_t 
#else
    #define THREAD_FD pthread_t
#endif 

//
void pipes_thread_start(void* fd, void* func, void* arg);
void pipes_thread_join(void* fd);

THREAD_FD pipes_thread_self();

int pipes_thread_exist(THREAD_FD fd);

int pipes_thread_detach(THREAD_FD fd);

// mutex
int pipes_thread_mutex_init(void* fd);
int pipes_thread_mutex_lock(void* fd);
int pipes_thread_mutex_unlock(void* fd);
int pipes_thread_mutex_destroy(void* fd);

// cond
int pipes_thread_cond_init(void*fd);
int pipes_thread_cond_wait(void*fdCond, void* fdMutex);

int pipes_thread_cond_timewait(void*fdCond, void* fdMutex, struct timeval*, uint32_t delayMs);

int pipes_thread_cond_signal(void*fd);
int pipes_thread_cond_destroy(void*fd);

#endif 






