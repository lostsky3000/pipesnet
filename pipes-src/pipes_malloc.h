
#ifndef PIPES_MALLOC_H
#define PIPES_MALLOC_H

#include <stddef.h>
#include <stdint.h>


void* pipes_malloc(size_t sz);
void* pipes_realloc(void* ptr, size_t sz);
void pipes_free(void* ptr);


char * pipes_strdup(const char *str);


//
void* pipes_msg_malloc(size_t sz);
void* pipes_msg_realloc(void* ptr, size_t sz);
void pipes_msg_free(void* ptr);

#endif 




