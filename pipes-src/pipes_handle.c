
#include "pipes_handle.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define MAX_SLOT_SIZE 0x40000000

struct handle_name {
	char * name;
	uint64_t handle;
};

static char * _strdup(const char *str, fn_malloc fn_malloc)
{
	size_t sz = strlen(str);
	char * ret = fn_malloc(sz + 1);
	memcpy(ret, str, sz + 1);
	return ret;
}

static uint64_t _handle_register(void * ctx, int64_t thread, struct handle_storage* s, int isSafe)
{
	if (isSafe)
	{
		rwlock_wlock(&s->lock);
	}
	for (;;) {
		int i;
		uint64_t handle = s->handle_index;
		for (i = 0; i < s->slot_size; i++, handle++) {
			if (handle > HANDLE_MASK) {
				// 0 is reserved
				handle = 1;
			}
			
			//int hash = handle & (s->slot_size - 1);
			uint64_t hash = handle % s ->slot_size;
			
			if (s->slot[hash] == NULL) {
				s->slot[hash] = ctx;
				s->handle_index = handle + 1;
				// assign harbor & thread
				handle = pipes_handle_encode(handle, s->harbor, thread);
				s->fn_set_ctx_handle(ctx, handle);
				if (isSafe)
				{
					rwlock_wunlock(&s->lock);
				}
				return handle;
			}
		}
		assert((s->slot_size * 2 - 1) <= HANDLE_MASK);
		void ** new_slot = s->fn_malloc(s->slot_size * 2 * s->sz_ctx_ptr);
		memset(new_slot, 0, s->slot_size * 2 * s->sz_ctx_ptr);
		for (i = 0; i < s->slot_size; i++) {
			//int hash = s->fn_get_ctx_handle(s->slot[i]) & (s->slot_size * 2 - 1);
			uint64_t h = s->fn_get_ctx_handle(s->slot[i]);
			uint64_t hash = h % (s->slot_size * 2);
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i];
		}
		s->fn_free(s->slot);
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

static void _handle_add_unsafe(void * ctx, uint64_t handle, struct handle_storage* s)
{
	for (;;) {
		int i;
		uint64_t hash = handle % s->slot_size;
		if (s->slot[hash] == NULL) {
			s->slot[hash] = ctx;
			// add succ
			return;
		}
		// expand
		assert((s->slot_size * 2 - 1) <= HANDLE_MASK);
		void ** new_slot = s->fn_malloc(s->slot_size * 2 * s->sz_ctx_ptr);
		memset(new_slot, 0, s->slot_size * 2 * s->sz_ctx_ptr);
		for (i = 0; i < s->slot_size; i++) {
			uint64_t h = s->fn_get_ctx_handle(s->slot[i]);
			uint64_t hash = h % (s->slot_size * 2);
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i];
		}
		s->fn_free(s->slot);
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

static void * _handle_grab(uint64_t handle, struct handle_storage* s, int isSafe)
{
	void * result = NULL;
	if (isSafe)
	{
		rwlock_rlock(&s->lock);
	}
	
	//uint32_t hash = handle & (s->slot_size - 1);
	uint64_t hash = handle % s->slot_size;
	
	void * ctx = s->slot[hash];
	if (ctx && s->fn_get_ctx_handle(ctx) == handle) {
		result = ctx;
	}
	if (isSafe)
	{
		rwlock_runlock(&s->lock);
	}
	return result;
}

static void
_insert_name_before(struct handle_storage *s, char *name, uint64_t handle, int before) {
	if (s->name_count >= s->name_cap) {
		s->name_cap *= 2;
		assert(s->name_cap <= MAX_SLOT_SIZE);
		struct handle_name * n = s->fn_malloc(s->name_cap * sizeof(struct handle_name));
		int i;
		for (i = 0; i < before; i++) {
			n[i] = s->name[i];
		}
		for (i = before; i < s->name_count; i++) {
			n[i + 1] = s->name[i];
		}
		s->fn_free(s->name);
		s->name = n;
	}
	else {
		int i;
		for (i = s->name_count; i > before; i--) {
			s->name[i] = s->name[i - 1];
		}
	}
	s->name[before].name = name;
	s->name[before].handle = handle;
	s->name_count++;
}
static const char *
_insert_name(struct handle_storage *s, const char * name, uint64_t handle) {
	int begin = 0;
	int end = s->name_count - 1;
	while (begin <= end) {
		int mid = (begin + end) / 2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c == 0) {
			return NULL;
		}
		if (c < 0) {
			begin = mid + 1;
		}
		else {
			end = mid - 1;
		}
	}
	char * result = _strdup(name, s->fn_malloc);

	_insert_name_before(s, result, handle, begin);

	return result;
}
static const char * _handle_namehandle(uint64_t handle, const char *name, struct handle_storage*s, int isSafe)
{
	if (isSafe)
	{
		rwlock_wlock(&s->lock);
	}

	const char * ret = _insert_name(s, name, handle);

	if (isSafe)
	{
		rwlock_wunlock(&s->lock);
	}

	return ret;
}

static uint64_t _handle_findname(const char * name, struct handle_storage* s, int isSafe) 
{
	if (isSafe)
	{
		rwlock_rlock(&s->lock);
	}

	uint64_t handle = 0;

	int begin = 0;
	int end = s->name_count - 1;
	while (begin <= end) {
		int mid = (begin + end) / 2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c == 0) {
			handle = n->handle;
			break;
		}
		if (c < 0) {
			begin = mid + 1;
		}
		else {
			end = mid - 1;
		}
	}
	if (isSafe)
	{
		rwlock_runlock(&s->lock);
	}

	return handle;
}

static int _handle_retire(uint64_t handle, struct handle_storage *s, int isSafe) {
	int ret = 0;
	if (isSafe)
	{
		rwlock_wlock(&s->lock);
	}

	//uint32_t hash = handle & (s->slot_size - 1);
	uint64_t hash = handle % s->slot_size;
	
	void * ctx = s->slot[hash];

	if (ctx != NULL && s->fn_get_ctx_handle(ctx) == handle) {
		s->slot[hash] = NULL;
		ret = 1;
		int i;
		int j = 0, n = s->name_count;
		for (i = 0; i < n; ++i) {
			if (s->name[i].handle == handle) {
				s->fn_free(s->name[i].name);
				continue;
			}
			else if (i != j) {
				s->name[j] = s->name[i];
			}
			++j;
		}
		s->name_count = j;
	}
	else {
		ctx = NULL;
	}
	if (isSafe)
	{
		rwlock_wunlock(&s->lock);
	}

	return ret;
}

static void _handle_retireall(struct handle_storage *s, int isSafe, void* udata, fn_handle_on_ctx_retire cb) 
{
	for (;;) {
		int n = 0;
		int i;
		for (i = 0; i < s->slot_size; i++) {
			if (isSafe)
			{
				rwlock_rlock(&s->lock);
			}
			void * ctx = s->slot[i];
			uint64_t handle = 0;
			if (ctx)
			{
				handle = s->fn_get_ctx_handle(ctx);
			}
			if (isSafe)
			{
				rwlock_runlock(&s->lock);
			}
			if (handle != 0) {
				if (_handle_retire(handle, s, isSafe)) 
				{
					if (cb)
					{
						cb(udata, ctx);
					}
					++n;
				}
			}
		}
		if (n == 0)
		{
			return;
		}
			
	}
}

int pipes_handle_add_unsafe(void*ctx, uint64_t handle, struct handle_storage*s)
{
	if (pipes_handle_grab_unsafe(handle, s) != NULL)  //exist, add failed
	{
		return 0;
	}
	_handle_add_unsafe(ctx, handle, s);
	return 1;
}

uint64_t pipes_handle_register_unsafe(void * ctx, int thread, struct handle_storage* s)
{
	return _handle_register(ctx, thread, s, 0);
}
uint64_t pipes_handle_register(void *ctx, int thread, struct handle_storage* s)
{
	return _handle_register(ctx, thread, s, 1);
}
void * pipes_handle_grab_unsafe(uint64_t handle, struct handle_storage* s)
{
	return _handle_grab(handle, s, 0);
};

void * pipes_handle_grab(uint64_t handle, struct handle_storage* s)
{
	return _handle_grab(handle, s, 1);
};

const char * pipes_handle_namehandle_unsafe(uint64_t handle, const char *name, struct handle_storage*s)
{
	return _handle_namehandle(handle, name, s, 0);
}

const char * pipes_handle_namehandle(uint64_t handle, const char *name, struct handle_storage*s)
{
	return _handle_namehandle(handle, name, s, 1);
}

uint64_t pipes_handle_findname_unsafe(const char * name, struct handle_storage* s)
{
	return _handle_findname(name, s, 0);
}

uint64_t pipes_handle_findname(const char * name, struct handle_storage* s)
{
	return _handle_findname(name, s, 1);
}

int pipes_handle_retire_unsafe(uint64_t handle, struct handle_storage*s)
{
	return _handle_retire(handle, s, 0);
}

int pipes_handle_retire(uint64_t handle, struct handle_storage*s)
{
	return _handle_retire(handle, s, 1);
}

void pipes_handle_retireall_unsafe(struct handle_storage *s)
{
	_handle_retireall(s, 0, NULL, NULL);
}
void pipes_handle_retireall(struct handle_storage *s)
{
	_handle_retireall(s, 1, NULL, NULL);
}

struct handle_storage* pipes_handle_new_storage(
	int harbor,
	int cap,
	fn_get_ctx_handle fn_get_ctx_handle, 
	fn_set_ctx_handle fn_set_ctx_handle,
	fn_malloc fn_malloc,
	fn_free fn_free,
	size_t sz_ctx_ptr)
{
	struct handle_storage*s = fn_malloc(sizeof(struct handle_storage));
	
	s->sz_ctx_ptr = sz_ctx_ptr;
	s->fn_get_ctx_handle = fn_get_ctx_handle;
	s->fn_set_ctx_handle = fn_set_ctx_handle;
	
	s->fn_malloc = fn_malloc;
	s->fn_free = fn_free;
	
	s->slot_size = cap;
	s->slot = fn_malloc(s->slot_size * sz_ctx_ptr);
	memset(s->slot, 0, s->slot_size * sz_ctx_ptr);
	
	rwlock_init(&s->lock);
	// reserve 0 for system
	s->harbor = harbor; //(uint64_t)(harbor & 0xff) << HANDLE_HARBOR_SHIFT;
	s->handle_index = 1;
	s->name_cap = 2;
	s->name_count = 0;
	s->name = fn_malloc(s->name_cap * sizeof(struct handle_name));
	
	return s;
};

static void _destroy_storage(struct handle_storage* s, int isSafe, void* udata, fn_handle_on_ctx_retire cb)
{
	if (isSafe)
	{
		rwlock_wlock(&s->lock);
	}
	//
	_handle_retireall(s, 0, udata, cb);
	fn_malloc fnMalloc = s->fn_malloc;
	fn_free fnFree = s->fn_free;
	fnFree(s->slot);
	fnFree(s->name);
	//
	if (isSafe)
	{
		rwlock_wunlock(&s->lock);
	}
	fnFree(s);
}
void pipes_handle_destroy_storage(struct handle_storage* s, void* udata, fn_handle_on_ctx_retire cb)
{
	_destroy_storage(s, 1, udata, cb);
}
void pipes_handle_destroy_storage_unsafe(struct handle_storage* s, void* udata, fn_handle_on_ctx_retire cb)
{
	_destroy_storage(s, 0, udata, cb);
}

uint64_t pipes_handle_reserve_handle(uint64_t harbor, uint64_t thread)
{
	return pipes_handle_encode(0, harbor, thread);
}
uint64_t pipes_handle_encode(uint64_t handleRaw, uint64_t harbor, uint64_t thread)
{ 
	handleRaw |= ((harbor & 0xff) << HANDLE_HARBOR_SHIFT);
	handleRaw |= (thread << HANDLE_THREAD_SHIFT);
	return handleRaw;
}

int pipes_handle_get_harbor(uint64_t handle)
{
	return handle >> HANDLE_HARBOR_SHIFT;
}

int pipes_handle_get_thread(uint64_t handle)
{
	return (handle >> HANDLE_THREAD_SHIFT) & 0xff;
}

uint32_t pipes_handle_id2local(uint64_t id)
{
	return id & 0xFFFFFFFF;
}
uint64_t pipes_handle_local2id(uint64_t localId, uint64_t harbor)
{
	localId |= ((harbor & 0xff) << HANDLE_HARBOR_SHIFT);
	return localId;
}

