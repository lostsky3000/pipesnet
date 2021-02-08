#include "pipes_runtime.h"

static int minservice_thread(struct pipes_global_context* g)
{
	int th = 0, min = 2100000000;
	int i;
	for (i=0; i<g->worker_thread_num; ++i)
	{
		int tmp = g->threads[i]->service_num;
		if (tmp < min)
		{
			min = tmp;
			th = i;
		}
	}
	return th;
}

int pipes_rt_alloc_thread(struct pipes_global_context* g, int policy)
{
	return minservice_thread(g);
}

