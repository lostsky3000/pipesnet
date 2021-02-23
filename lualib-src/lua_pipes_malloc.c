
#include "lua_pipes_malloc.h"
#include "pipes_malloc.h"
#include "atomic.h"
#include <assert.h>

void* luapps_malloc(size_t sz)
{
	return pipes_malloc(sz);
	
	// refcnt ptr
	/*
	void* ptr = pipes_malloc(LPPS_BUF_HEAD_LEN + sz);
	*(int32_t*)ptr = 1;
	return ptr + LPPS_BUF_HEAD_LEN;
	*/
}

int luapps_free(void* ptr, int ref)
{
	pipes_free(ptr);
	return 1;
	
	// refcnt ptr
	/*
	assert(ref > 0);
	ptr -= LPPS_BUF_HEAD_LEN;
	int cur = 0;
	while (--ref >= 0)
	{
		cur = ATOM_DEC((int32_t*)ptr);
		if (cur == 0)
		{
			pipes_free(ptr);
			break;
		}
		else if (cur < 0)  // exception
		{
			break;
		}
	}
	return cur;
	*/
}

int luapps_retain(void* ptr, int ref)
{
	assert(0);
	
	// refcnt ptr
	/*
	assert(ref > 0);
	ptr -= LPPS_BUF_HEAD_LEN;
	int cur = 0;
	while (--ref >= 0)
	{
		cur = ATOM_INC((int32_t*)ptr);
		if (cur <= 1)  //invalid
		{
			break;
		}
	}
	return cur;
	*/
}




