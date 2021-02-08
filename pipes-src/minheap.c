
#include "minheap.h"

#include <string.h>
#include <stdint.h>

struct minheap_node
{
	int idx;
	void* udata;
};

struct minheap_queue
{
	int node_cap;
	struct minheap_node* nodes;
	int node_num;
	
	fn_minheap_compare fn_compare;
	fn_minheap_malloc fn_malloc;
	fn_minheap_free fn_free;
};


//
struct minheap_queue* minheap_create_queue(
	int nodeCap, 
	fn_minheap_compare fnCompare,
	fn_minheap_malloc fnMalloc,
	fn_minheap_free fnFree)
{
	struct minheap_queue* queue = NULL;
	size_t sz = sizeof(struct minheap_queue);
	queue = fnMalloc(sz);
	memset(queue, 0, sz);
	
	queue->fn_compare = fnCompare;
	queue->fn_malloc = fnMalloc;
	queue->fn_free = fnFree;
	
	queue->node_cap = nodeCap;
	sz = sizeof(struct minheap_node) * queue->node_cap;
	queue->nodes = queue->fn_malloc(sz);
	memset(queue->nodes, 0, sz);
	int i;
	for (i = 0; i < queue->node_cap; ++i)
	{
		queue->nodes[i].idx = i;
	}
	return queue;
};

static void shift_up(struct minheap_queue* queue)
{
	struct minheap_node* nodes = queue->nodes;
	int id = queue->node_num;
	void* udata;
	while (id > 1)  // has not arrive root yet
	{
		int upId = id >> 1;
		if (queue->fn_compare(nodes[id - 1].udata, nodes[upId - 1].udata) < 0)  // smaller than up, swap
			{
				udata = nodes[id - 1].udata;
				nodes[id - 1].udata = nodes[upId - 1].udata;
				nodes[upId - 1].udata = udata;
				id = upId;
			}
		else  //stop
		{
			break;
		}
	}
}
static void shift_down(struct minheap_queue* queue)
{
	struct minheap_node* nodes = queue->nodes;
	fn_minheap_compare fnCompare = queue->fn_compare;
	int id = 1;	
	int downId;
	void* udata;
	while ( (downId=id<<1) <= queue->node_num )
	{
		if( downId < queue->node_num ) {  //has right-child, compare
			if(fnCompare(nodes[downId-1].udata, nodes[downId].udata) > 0) //bigger than right, choose smaller
			{
				++downId;
			}
		}
		if (fnCompare(nodes[downId - 1].udata, nodes[id - 1].udata) < 0) // bigger than child, swap
		{
			udata = nodes[downId - 1].udata;
			nodes[downId - 1].udata = nodes[id - 1].udata;
			nodes[id - 1].udata = udata;
			id = downId;
		}
		else
		{
			break;
		}
	}
}
void minheap_add_node(void* udata, struct minheap_queue* queue)
{
	if (queue->node_num >= queue->node_cap) //queue is full
	{
		queue->node_cap *= 2;
		struct minheap_node* oldNodes = queue->nodes;
		size_t sz = sizeof(struct minheap_node) * queue->node_cap;
		queue->nodes = queue->fn_malloc(sz);
		memcpy(queue->nodes, oldNodes, sizeof(struct minheap_node) * queue->node_num);
		queue->fn_free(oldNodes);
	}
	queue->nodes[queue->node_num].udata = udata;
	++queue->node_num;
	//
	shift_up(queue);
}

void* minheap_get_min(struct minheap_queue* queue)
{
	if (queue->node_num < 1) // empty
	{
		return NULL;
	}
	return queue->nodes[0].udata;
}

void* minheap_pop_min(struct minheap_queue* queue)
{
	if (queue->node_num < 1) // empty
	{
		return NULL;
	}
	void* udata = queue->nodes[0].udata;
	--queue->node_num;
	//resort
	if(queue->node_num > 0)  // still has node, resort
	{
		queue->nodes[0].udata = queue->nodes[queue->node_num].udata;
		shift_down(queue);
	}
	return udata;
}
