
#include "pipes_mq.h"

#include "pipes.h"
#include "pipes_malloc.h"
#include <string.h>
#include <stdio.h>

#define DEFAULT_QUEUE_SIZE 64

static int _mq_pop_msg(struct message_queue* q, struct pipes_message* m)
{
	if (q->head != q->tail)  //has un-consumed msg
	{
		*m = q->queue[q->head++];
		q->head = q->head % q->cap;
		return 1;
	}
	return 0;
}

static void _expand_msg_queue(struct message_queue* q)
{
	int newCap = q->cap * 2;
	struct pipes_message* newQueue = pipes_malloc(sizeof(struct pipes_message) * newCap);
	
	int i;
	for (i=0; i<q->cap; ++i)
	{
		newQueue[i] = q->queue[(q->head + i) % q->cap];
	}
	
	q->head = 0;
	q->tail = q->cap;
	q->cap = newCap;
	
	pipes_free(q->queue);
	q->queue = newQueue;
	
}
static struct pipes_message* _mq_allocmsg_tail(struct message_queue* q)
{
	struct pipes_message* msg = NULL;
	msg = &q->queue[q->tail++];
	if (q->tail >= q->cap)
	{
		q->tail = 0;
	}
	if (q->tail == q->head)  //full, expand
	{
		_expand_msg_queue(q);
		msg = &q->queue[q->tail - 1];
	}
	return msg;
}

static struct pipes_message* _mq_allocmsg_head(struct message_queue* q)
{
	if (--q->head < 0)   // cycle
	{
		q->head = q->cap - 1;
	}
	if (q->head == q->tail) //full, expand
		{
			_expand_msg_queue(q);
		}
	return &q->queue[q->head];
}


void pipes_mq_push_unsafe(struct message_queue*q, struct pipes_message*msg)
{
	struct pipes_message* m = _mq_allocmsg_tail(q);
	*m = *msg;
}
void pipes_mq_push(struct message_queue*q, struct pipes_message*msg)
{
	SPIN_LOCK(q);
	pipes_mq_push_unsafe(q, msg);
	SPIN_UNLOCK(q);
}

void pipes_mq_inserthead_unsafe(struct message_queue*q, struct pipes_message*msg)
{
	struct pipes_message* m = _mq_allocmsg_head(q);
	*m = *msg;
}
void pipes_mq_inserthead(struct message_queue*q, struct pipes_message*msg)
{
	SPIN_LOCK(q);
	pipes_mq_inserthead_unsafe(q, msg);
	SPIN_UNLOCK(q);
}

int pipes_mq_pop_unsafe(struct message_queue*q, struct pipes_message*m)
{
	return _mq_pop_msg(q, m);
}
int pipes_mq_pop(struct message_queue*q, struct pipes_message*m)
{
	int ret;
	SPIN_LOCK(q);
	ret = _mq_pop_msg(q, m);
	SPIN_UNLOCK(q);
	return ret;
}

int pipes_mq_size_unsafe(struct message_queue*q)
{
	if (q->head != q->tail)
	{
		int off = q->tail - q->head;
		return off > 0 ? off : q->cap + off;
	}
	else
	{
		return 0;
	}
}
int pipes_mq_size(struct message_queue*q)
{
	int sz;
	SPIN_LOCK(q);
	sz = pipes_mq_size_unsafe(q);
	SPIN_UNLOCK(q);
	return sz;
}

struct message_queue* pipes_mq_create_cap(int cap)
{
	size_t sz = sizeof(struct message_queue);
	struct message_queue* q = pipes_malloc(sz);
	memset(q, 0, sz);
	pipes_mq_init(q, cap);
	return q;
};

struct message_queue* pipes_mq_create()
{	
	return pipes_mq_create_cap(DEFAULT_QUEUE_SIZE);
};

void pipes_mq_init(struct message_queue*q, int cap)
{
	q->head = 0;
	q->tail = 0;
	q->cap = cap;
	SPIN_INIT(q);
	//
	q->queue = pipes_malloc(sizeof(struct pipes_message) * q->cap);
}


//=================== swap mq
struct swap_message_queue
{
	struct message_queue queue_a;
	struct message_queue queue_b;
	struct message_queue* write_q;
	struct message_queue* read_q;
}
;

void pipes_smq_push(struct swap_message_queue*q, struct pipes_message*m)
{
	SPIN_LOCK((struct message_queue*)q);
	pipes_mq_push_unsafe(q->write_q, m);
	//printf("pipes_smq_push, smq=%p, wq=%p, szwq=%d\n", q, q->write_q, pipes_mq_size_unsafe(q->write_q));   // debug
	SPIN_UNLOCK((struct message_queue*)q);
}
void pipes_smq_inserthead(struct swap_message_queue*q, struct pipes_message*m)
{
	SPIN_LOCK((struct message_queue*)q);
	pipes_mq_inserthead_unsafe(q->write_q, m);
	SPIN_UNLOCK((struct message_queue*)q);
}
void pipes_smq_push_batch_begin(struct swap_message_queue*q)
{
	SPIN_LOCK((struct message_queue*)q);
}
void pipes_smq_push_batch_end(struct swap_message_queue*q)
{
	SPIN_UNLOCK((struct message_queue*)q);
}
void pipes_smq_push_batch_exec(struct swap_message_queue*q, struct pipes_message*m)
{
	pipes_mq_push_unsafe(q->write_q, m);
}

int pipes_smq_swap_by_read(struct swap_message_queue*q)
{
	int sz;
	SPIN_LOCK((struct message_queue*)q);
	sz = pipes_mq_size_unsafe(q->write_q);
	//printf("smq swap by read, smq=%p, wq=%p, szwq=%d\n", q, q->write_q, sz);  // debug
	if (sz > 0)   // write queue has msg, swap
	{
		struct message_queue* preWrite = q->write_q;
		q->write_q = q->read_q;
		q->read_q = preWrite;
	}
	SPIN_UNLOCK((struct message_queue*)q);
	return sz;
}

int pipes_smq_pop_unsafe(struct swap_message_queue*q, struct pipes_message*m)
{
	int ret = pipes_mq_pop_unsafe(q->read_q, m);
	//printf("smq pop, ret=%d, smq=%p, rq=%p\n", ret, q, q->read_q);  // debug
	return ret;
}

struct message_queue* pipes_smq_cur_readqueue(struct swap_message_queue*q)
{
	return q->read_q;
};

struct swap_message_queue* pipes_smq_create(int cap)
{
	struct swap_message_queue* q = pipes_malloc(sizeof(struct swap_message_queue));
	pipes_mq_init(&q->queue_a, cap);
	pipes_mq_init(&q->queue_b, cap);
	q->write_q = &q->queue_a;
	q->read_q = &q->queue_b;
	
	return q;
};
void pipes_smq_destroy_unsafe(struct swap_message_queue* smq)
{
	pipes_free(smq->queue_a.queue);
	pipes_free(smq->queue_b.queue);
	pipes_free(smq);
}

//
struct message_queue* pipes_worker_mq_pop(struct worker_message_queue*q)
{ 
	struct message_queue* mq = q->head;
	if (mq)
	{
		q->head = mq->next;
		if (q->head == NULL)   // worker queue is empty
		{
			q->tail = NULL;
		}
		mq->next = NULL;
		mq->in_parent = MQ_NOTIN_PARENT;
		--q->queue_num;
	}
	
	return mq;
}
int pipes_worker_mq_push(struct worker_message_queue*q, struct message_queue*mq)
{
	if (mq->in_parent == MQ_IN_PARENT)   // already in parent queue
	{
		return 1;
	}
	if (pipes_mq_size_unsafe(mq) < 1)  // mq is empty
	{
		return 0;
	}
	if (q->tail != NULL)  // worker queue not empty
	{
		q->tail->next = mq;
		q->tail = mq;
	}
	else // worker queue is empty
	{
		q->head = mq;
		q->tail = mq;
	}
	mq->next = NULL;
	mq->in_parent = MQ_IN_PARENT;
	++q->queue_num;
	
	return 2;
}
