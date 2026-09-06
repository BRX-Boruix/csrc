/* pthread.h - BORUIX freestanding C pthread subset (T2-3..T2-5, threads.md).
 * Minimal POSIX-ish pthread over the kernel thread_spawn/join + per-thread descriptor.
 * S09 honesty: pthread_t is an opaque pointer to a runtime descriptor (registry slot);
 * retval is carried via the descriptor (kernel join returns only an integer exit code);
 * pthread_join may be called only from the group leader (kernel reap restriction).
 */
#ifndef BORUIX_PTHREAD_H
#define BORUIX_PTHREAD_H

typedef unsigned long pthread_t;

/* Opaque attr (attributes not yet supported: create ignores it). */
typedef struct pthread_attr_t { int _unused; } pthread_attr_t;

/* Thread states. */
enum { PTH_ST_RUNNING = 1, PTH_ST_EXITED = 2, PTH_ST_DETACHED = 3 };

int pthread_create(pthread_t* t, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);
int pthread_join(pthread_t t, void** retval);
int pthread_detach(pthread_t t);
pthread_t pthread_self(void);
void pthread_exit(void* retval); /* noreturn */
int pthread_equal(pthread_t a, pthread_t b);

/* ---- T2-4 sync object types ---- */
/* Complete (not opaque) so programs can declare sync objects as globals/stack. Each holds a
 * user atomic state/field plus the kernel SYNC park id used to block/wake. */
typedef struct bx_mutex {
    volatile int state;    /* 0 unlocked,1 locked(no waiter),2 locked(may have waiter) */
    volatile unsigned long epoch; /* bumped on each unlock-with-waiter; mirrored to SYNC word */
    unsigned long park_id; /* kernel SYNC word; word value == epoch (never 0-only) */
    int _init;
} pthread_mutex_t;
typedef struct bx_cond {
    volatile int gen;      /* user generation counter (bumped on signal/broadcast) */
    unsigned long park_id;
    int _init;
} pthread_cond_t;
typedef struct bx_sem {
    volatile int count;
    volatile unsigned long epoch; /* bumped on each post; mirrored to SYNC word */
    unsigned long park_id;
    int _init;
} bx_sem_t;

/* ---- mutex ---- */
int pthread_mutex_init(pthread_mutex_t* m, const void* attr);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_trylock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_mutex_destroy(pthread_mutex_t* m);

/* ---- condvar ---- */
int pthread_cond_init(pthread_cond_t* c, const void* attr);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_cond_destroy(pthread_cond_t* c);

/* ---- semaphore ---- */
int sem_init(bx_sem_t* s, int pshared, unsigned int value);
int sem_wait(bx_sem_t* s);
int sem_trywait(bx_sem_t* s);
int sem_post(bx_sem_t* s);
int sem_destroy(bx_sem_t* s);
int sem_getvalue(bx_sem_t* s, int* val);

#endif
