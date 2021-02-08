
#include "pipes_malloc.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "atomic.h"

//#define DEBUG_PIPES_MALLOC
//
#define NO_REF_CNT_FLAG -121
#define NO_REF_CNT_FLAG_FREED -123
#define REF_CNT_FLAG -111

static int checkPtrFlag(void* ptr)
{
	if (ptr)
	{
		size_t sz = *(unsigned int*)(ptr - 6);
		char head = *(char*)(ptr - 1);
		char tail = *(char*)(ptr + sz);
		if (head != -100 || tail != -100)
		{
			int n = 1;
		}
	}
	return 0;
}

static void debug_free(void* ptr);
static void* initPtr(void* ptr, size_t sz)
{
	// dataLen(4byte), freeCnt(1byte), headFlag(1byte), ...data..., tailFlag(1byte)
	*(unsigned int*)ptr = sz;    // mark dataLen
	*(unsigned char*)(ptr + 4) = 0;  // init freeCnt
	*(char*)(ptr + 5) = -100;   // init headFlag
	*(char*)(ptr + sz + 6) = -100;    // init tailFlag
	
	return ptr + 6;
}
static void* debug_malloc(size_t sz)
{
	// dataLen(4byte), freeCnt(1byte), headFlag(1byte), ...data..., tailFlag(1byte)
	void* ptr = malloc(sz + 7);
	
	ptr = initPtr(ptr, sz);
	
	checkPtrFlag(ptr);
	
	return ptr;
}
static void* debug_realloc(void* ptr, size_t sz)
{
	checkPtrFlag(ptr);
	//
	void* ptrNew = realloc(ptr?ptr - 6:NULL, sz + 7);
	if (ptrNew)  // realloc succ, init ptr
	{	
		ptrNew = initPtr(ptrNew, sz);
	}
	checkPtrFlag(ptrNew);
	return ptrNew;
}
static void debug_free(void* ptr)
{
	checkPtrFlag(ptr);
	//
	if (ptr)
	{
		int freeCnt = *(unsigned char*)(ptr - 2);
		if (++freeCnt > 1)  //duplicate free
		{
			int n = 1;	
		}
		*(unsigned char*)(ptr - 2) = freeCnt;
	}
}

//
void* pipes_malloc(size_t sz)
{
#ifdef DEBUG_PIPES_MALLOC
	return debug_malloc(sz);
#else
	return malloc(sz);
	//return malloc(sz);
#endif
}
void* pipes_realloc(void* ptr, size_t sz)
{	
#ifdef DEBUG_PIPES_MALLOC
	return debug_realloc(ptr, sz);
#else
	return realloc(ptr, sz);
	//return realloc(ptr, sz);
#endif
}
void pipes_free(void* ptr)
{	
#ifdef DEBUG_PIPES_MALLOC
	debug_free(ptr);
#else
	free(ptr);
#endif
}

//
void* pipes_msg_malloc(size_t sz)
{
	return pipes_malloc(sz);
	//return malloc(sz);
}
void* pipes_msg_realloc(void* ptr, size_t sz)
{
	return pipes_realloc(ptr, sz);
	//return realloc(ptr, sz);
}
void pipes_msg_free(void* ptr)
{
	pipes_free(ptr);
	//free(ptr);
}


//
char * pipes_strdup(const char *str)
{
	size_t sz = strlen(str);
	char * ret = pipes_malloc(sz + 1);
	memcpy(ret, str, sz + 1);
	return ret;
}

