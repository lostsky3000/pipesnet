#include "pipes_time.h"


void pipes_time_now(struct timeval* tm)
{
#ifdef SYS_IS_LINUX
	gettimeofday(tm, NULL);
#else
	
#endif // SYS_IS_LINUX

}

uint64_t pipes_time_toms(struct timeval* tm)
{
#ifdef SYS_IS_LINUX
	return (tm->tv_sec * 1000000 + tm->tv_usec) / 1000;
#else
	
#endif // SYS_IS_LINUX	
	return 0;
}


