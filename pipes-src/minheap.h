#ifndef MINHEAP_H
#define MINHEAP_H 
#include <stdlib.h>
typedef int(*fn_minheap_compare)(void*, void*);
typedef void*(*fn_minheap_malloc)(size_t);
typedef void(*fn_minheap_free)(void*);

struct minheap_queue;


struct minheap_queue* minheap_create_queue(
	int nodeCap, 
	fn_minheap_compare fnCompare,
	fn_minheap_malloc fnMalloc,
	fn_minheap_free fnFree);

void minheap_add_node(void* udata, struct minheap_queue* queue);

void* minheap_get_min(struct minheap_queue* queue);

void* minheap_pop_min(struct minheap_queue* queue);

#endif // !MINHEAP_H


