

#include "pipes_thread.h"

#ifdef SYS_IS_LINUX
#include <signal.h>
#endif // SYS_IS_LINUX


// mutex
int pipes_thread_mutex_init(void* fd)
{
#ifdef SYS_IS_LINUX
	pthread_mutex_t* mtx = (pthread_mutex_t*)fd;
	return pthread_mutex_init(mtx, NULL);
#endif

}
int pipes_thread_mutex_lock(void* fd)
{
#ifdef SYS_IS_LINUX
	pthread_mutex_t* mtx = (pthread_mutex_t*)fd;
	return pthread_mutex_lock(mtx);
#endif
}
int pipes_thread_mutex_unlock(void* fd)
{
#ifdef SYS_IS_LINUX
	pthread_mutex_t* mtx = (pthread_mutex_t*)fd;
	return pthread_mutex_unlock(mtx);
#endif
}
int pipes_thread_mutex_destroy(void* fd)
{
#ifdef SYS_IS_LINUX
	pthread_mutex_t* mtx = (pthread_mutex_t*)fd;
	return pthread_mutex_destroy(mtx);
#endif

}

// cond
int pipes_thread_cond_init(void*fd)
{
#ifdef SYS_IS_LINUX
	pthread_cond_t* cond = (pthread_cond_t*)fd;
	return pthread_cond_init(cond, NULL);
#endif
}
int pipes_thread_cond_wait(void*fdCond, void* fdMutex)
{
#ifdef SYS_IS_LINUX
	pthread_cond_t* cond = (pthread_cond_t*)fdCond;
	pthread_mutex_t* mtx = (pthread_mutex_t*)fdMutex;
	return pthread_cond_wait(cond, mtx);
#endif
}

int pipes_thread_cond_timewait(void*fdCond, void* fdMutex, struct timeval* now, uint32_t delayMs)
{
#ifdef SYS_IS_LINUX
	pthread_cond_t* cond = (pthread_cond_t*)fdCond;
	pthread_mutex_t* mtx = (pthread_mutex_t*)fdMutex;
	
	struct timespec expire;
	expire.tv_sec = now->tv_sec + delayMs / 1000;
	expire.tv_nsec = (now->tv_usec + delayMs % 1000) * 1000;
	
	return pthread_cond_timedwait(cond, mtx, &expire);
#endif
}

int pipes_thread_cond_signal(void*fd)
{
#ifdef SYS_IS_LINUX
	pthread_cond_t* cond = (pthread_cond_t*)fd;
	return pthread_cond_signal(cond);
#endif
}
int pipes_thread_cond_destroy(void*fd)
{
#ifdef SYS_IS_LINUX
	pthread_cond_t* cond = (pthread_cond_t*)fd;
	return pthread_cond_destroy(cond);
#endif
}

// thread
void pipes_thread_start(void * fd, void* func, void* arg)
{
	
#ifdef SYS_IS_LINUX
	pthread_create((pthread_t*)fd, NULL, func, arg);			  
#endif 

}
THREAD_FD pipes_thread_self()
{
#ifdef SYS_IS_LINUX
	return pthread_self(); 
#endif
	return 0;
}

int pipes_thread_exist(THREAD_FD fd)
{
#ifdef SYS_IS_LINUX
	int ret = pthread_kill(fd, 0);
	if (ret != 0)  // thread not exist
	{
		return 0;
	}
	return 1;
#endif
	return -1;
}

int pipes_thread_detach(THREAD_FD fd)
{
#ifdef SYS_IS_LINUX
	pthread_detach(fd);
	return 1;
#endif // SYS_IS_LINUX
	
	return 0;
}

void pipes_thread_join(void* fd)
{
#ifdef  SYS_IS_LINUX
	pthread_join(*(pthread_t*)fd, NULL);				  
#endif 

}


