#ifndef _HIVE_SPINLOCK_H_
#define _HIVE_SPINLOCK_H_


struct spinlock {
	int lock;
};

static inline void
spinlock_init(struct spinlock *lock) {
	lock->lock = 0;
}

static inline void
spinlock_lock(struct spinlock *lock) {
	while (__sync_lock_test_and_set(&lock->lock,1)) {}
}

static inline int
spinlock_trylock(struct spinlock *lock) {
	return __sync_lock_test_and_set(&lock->lock,1) == 0;
}

static inline void
spinlock_unlock(struct spinlock *lock) {
	__sync_lock_release(&lock->lock);
}

#endif