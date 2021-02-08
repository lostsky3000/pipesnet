
#ifndef PIPES_HANDLE_H
#define PIPES_HANDLE_H

#include "rwlock.h"
#include <stdint.h>
#include <stddef.h>

// reserve high 8 bits for remote id
#define HANDLE_MASK 0xffffff
#define HANDLE_THREAD_SHIFT 24
#define HANDLE_HARBOR_SHIFT 32


struct handle_name;

typedef uint64_t(*fn_get_ctx_handle)(void*);
typedef void(*fn_set_ctx_handle)(void*, uint64_t);

typedef void*(*fn_malloc)(size_t);
typedef void(*fn_free)(void*);

typedef void(*fn_handle_on_ctx_retire)(void*, void*);


struct handle_storage {
	struct rwlock lock;

	int harbor;
	uint32_t handle_index;
	int slot_size;
	void ** slot;
	size_t sz_ctx_ptr;
	
	int name_cap;
	int name_count;
	
	struct handle_name *name;
	
	fn_get_ctx_handle fn_get_ctx_handle;
	fn_set_ctx_handle fn_set_ctx_handle;
	
	fn_malloc fn_malloc;
	fn_free fn_free;
	
};

struct handle_storage* pipes_handle_new_storage(
	int harbor, int cap, 
	fn_get_ctx_handle fn_get_ctx_handle, 
	fn_set_ctx_handle fn_set_ctx_handle,
	fn_malloc fn_malloc, fn_free fn_free, size_t sz_ctx_ptr);

void pipes_handle_destroy_storage(struct handle_storage*, void*, fn_handle_on_ctx_retire);
void pipes_handle_destroy_storage_unsafe(struct handle_storage*, void*, fn_handle_on_ctx_retire);

//
uint64_t pipes_handle_register_unsafe(void *ctx, int thread, struct handle_storage *s);
uint64_t pipes_handle_register(void *ctx, int thread, struct handle_storage *s);

int pipes_handle_add_unsafe(void*ctx, uint64_t handle, struct handle_storage*s);

//
void * pipes_handle_grab_unsafe(uint64_t handle, struct handle_storage*s);
void * pipes_handle_grab(uint64_t handle, struct handle_storage*s);

//
const char * pipes_handle_namehandle_unsafe(uint64_t handle, const char *name, struct handle_storage*s);
const char * pipes_handle_namehandle(uint64_t handle, const char *name, struct handle_storage*s);

//
uint64_t pipes_handle_findname_unsafe(const char * name, struct handle_storage*s);
uint64_t pipes_handle_findname(const char * name, struct handle_storage*s);

//
int pipes_handle_retire_unsafe(uint64_t handle, struct handle_storage*s);
int pipes_handle_retire(uint64_t handle, struct handle_storage*s);

//
void pipes_handle_retireall_unsafe(struct handle_storage *s); 
void pipes_handle_retireall(struct handle_storage *s); 


//
uint64_t pipes_handle_encode(uint64_t handleRaw, uint64_t harbor, uint64_t thread);
int pipes_handle_get_harbor(uint64_t handle);
int pipes_handle_get_thread(uint64_t handle);

uint64_t pipes_handle_reserve_handle(uint64_t harbor, uint64_t thread);


uint64_t pipes_handle_local2id(uint64_t localId, uint64_t harbor);
uint32_t pipes_handle_id2local(uint64_t id);
#endif 




