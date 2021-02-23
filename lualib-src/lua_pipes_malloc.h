#ifndef LUA_PIPES_MALLOC_H
#define LUA_PIPES_MALLOC_H

#include <stdlib.h>

#define LPPS_BUF_HEAD_LEN 4

void* luapps_malloc(size_t sz);

int luapps_free(void* ptr, int ref);

int luapps_retain(void* ptr, int ref);


#endif 
