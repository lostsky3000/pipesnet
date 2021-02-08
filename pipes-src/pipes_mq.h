
#ifndef PIPES_MQ_H
#define PIPES_MQ_H		 

#include "spinlock.h"

#include <stdint.h>

#define MQ_IN_PARENT 1
#define MQ_NOTIN_PARENT 0

struct pipes_message;

struct message_queue
{
	struct spinlock lock;
	int head;
	int tail;
	int cap;
	int in_parent;
	struct pipes_message* queue;
	//
	struct message_queue* next;
	void* udata;
};

struct worker_message_queue
{
	struct message_queue* head;
	struct message_queue* tail;
	int queue_num;
};
//
struct message_queue* pipes_worker_mq_pop(struct worker_message_queue*);
int pipes_worker_mq_push(struct worker_message_queue*, struct message_queue*);


//
struct message_queue* pipes_mq_create();
struct message_queue* pipes_mq_create_cap(int cap);
void pipes_mq_init(struct message_queue*, int cap);
//
void pipes_mq_push_unsafe(struct message_queue*, struct pipes_message*);
void pipes_mq_push(struct message_queue*, struct pipes_message*);
//
void pipes_mq_inserthead_unsafe(struct message_queue*, struct pipes_message*);
void pipes_mq_inserthead(struct message_queue*, struct pipes_message*);
//
int pipes_mq_pop_unsafe(struct message_queue*, struct pipes_message*);
int pipes_mq_pop(struct message_queue*, struct pipes_message*);
//
int pipes_mq_size_unsafe(struct message_queue*);
int pipes_mq_size(struct message_queue*);




// ============ swap mq
struct swap_message_queue;
// push
void pipes_smq_push(struct swap_message_queue*, struct pipes_message*);
// insert-head
void pipes_smq_inserthead(struct swap_message_queue*, struct pipes_message*);
// batch push
void pipes_smq_push_batch_begin(struct swap_message_queue*);
void pipes_smq_push_batch_end(struct swap_message_queue*);
void pipes_smq_push_batch_exec(struct swap_message_queue*, struct pipes_message*);
// pop
int pipes_smq_pop_unsafe(struct swap_message_queue*, struct pipes_message*);
// swap
int pipes_smq_swap_by_read(struct swap_message_queue*);
//
struct message_queue* pipes_smq_cur_readqueue(struct swap_message_queue*);
// create
struct swap_message_queue* pipes_smq_create(int cap);
// destroy
void pipes_smq_destroy_unsafe(struct swap_message_queue*);

#endif



