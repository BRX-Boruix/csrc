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

#endif
