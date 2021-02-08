#ifndef PIPES_SPINLOCK_H
#define PIPES_SPINLOCK_H

#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);
#define SPIN_TRYLOCK(q) spinlock_trylock(&(q)->lock);

struct spinlock {
	int lock;
};

static inline void
spinlock_init(struct spinlock *lock) {
	lock->lock = 0;
}

static inline void
spinlock_lock(struct spinlock *lock) {
	while (__sync_lock_test_and_set(&lock->lock, 1)) {}
}

static inline int
spinlock_trylock(struct spinlock *lock) {
	return __sync_lock_test_and_set(&lock->lock, 1) == 0;
}

static inline void
spinlock_unlock(struct spinlock *lock) {
	__sync_lock_release(&lock->lock);
}

static inline void
spinlock_destroy(struct spinlock *lock) {
	(void) lock;
}



// spinlock int

#define SPIN_INIT_INT(pnum) spinlock_init_int(pnum);
#define SPIN_LOCK_INT(pnum) spinlock_lock_int(pnum);
#define SPIN_UNLOCK_INT(pnum) spinlock_unlock_int(pnum);
#define SPIN_DESTROY_INT(pnum) spinlock_destroy_int(pnum);
static inline void
spinlock_init_int(int* lock) {
	*lock = 0;
}
static inline void
spinlock_lock_int(int* lock) {
	while (__sync_lock_test_and_set(lock, 1)) {}
}
static inline void
spinlock_unlock_int(int* lock) {
	__sync_lock_release(lock);
}
static inline void
spinlock_destroy_int(int* lock) {
	(void) lock;
}


#endif




