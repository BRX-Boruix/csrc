/* pthread_sync.c - BORUIX freestanding C pthread mutex/condvar/semaphore (T2-4).
 * Implements pthread_mutex_*, pthread_cond_*, sem_* over user-space atomics + the kernel
 * SYNC word (ADR-032) as the park/unpark primitive (DESIGN-T2 SS5, zero kernel changes).
 * The kernel sync word is used purely to park/wake; its value is kept at 0 and
 * sync_wake(id,0,n) pops up to n parked waiters; sync_wait(id,0,0) blocks one.
 *
 * Mutex: atomic state 0=unlocked,1=locked(nobody waiting),2=locked(may have waiters).
 *   lock  : CAS 0->1 fast; else set state 2 then park until woken, then retry.
 *   unlock: exchange->0; if old==2 wake one.
 * Condvar: user generation counter (atomic) + park sync word. waiter snapshots gen under
 *   the mutex, unlocks, then loops { if gen changed -> proceed; else park }. signal bumps
 *   gen then wakes one; broadcast bumps gen then wakes all. Because gen is bumped before
 *   the wake and re-checked before each park, a signal in the unlock->park window is
 *   caught (gen already changed) and not lost.
 * Sem: atomic count; wait: loop try dec(>0) else park; post: inc then wake one.
 */
#include "boruix.h"
#include "pthread.h"

/* ---- user-space atomics via clang builtins (LOCK-prefixed insns at CPL3) ---- */
static inline int lk_ld(const volatile int* p)  { return __atomic_load_n((const int*)p, __ATOMIC_ACQUIRE); }
static inline int ld_rlx(const volatile int* p) { return __atomic_load_n((const int*)p, __ATOMIC_RELAXED); }
static inline int cas_int(volatile int* p, int exp, int des) {
    int e = exp; return __atomic_compare_exchange_n(p, &e, des, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}
static inline int xchg_int(volatile int* p, int v) { return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL); }
static inline void park(ulong id) { (void)boruix_sync_wait(id, 0, 0); }
static inline void unpark(ulong id, ulong n) { (void)boruix_sync_wake(id, 0, n); }

/* ---- mutex ---- */
int pthread_mutex_init(pthread_mutex_t* m, const void* attr) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    (void)attr;
    ulong id = boruix_sync_create(0);
    if ((long)id < 0) return -1;
    p->state = 0; p->park_id = id; p->_init = 1;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    if (!p->_init) return -1;
    if (cas_int(&p->state, 0, 1)) return 0;          /* fast: uncontended */
    for (;;) {
        if (ld_rlx(&p->state) == 2 || !cas_int(&p->state, 1, 2)) {
            park(p->park_id);                         /* block until unlocked */
        }
        if (cas_int(&p->state, 0, 2)) return 0;       /* acquired (keep waiters flag) */
    }
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    if (!p->_init) return -1;
    return cas_int(&p->state, 0, 1) ? 0 : 1;          /* 1 = busy */
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    int old = xchg_int(&p->state, 0);
    if (old == 2) unpark(p->park_id, 1);              /* wake one waiter */
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* m) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    (void)boruix_sync_delete(p->park_id);
    p->_init = 0;
    return 0;
}

/* ---- condvar ---- */
int pthread_cond_init(pthread_cond_t* c, const void* attr) {
    struct bx_cond* p = (struct bx_cond*)c;
    (void)attr;
    ulong id = boruix_sync_create(0);
    if ((long)id < 0) return -1;
    p->gen = 0; p->park_id = id; p->_init = 1;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
    struct bx_cond* p = (struct bx_cond*)c;
    if (!p->_init) return -1;
    int mygen = ld_rlx(&p->gen);                      /* snapshot under mutex */
    pthread_mutex_unlock(m);
    for (;;) {
        if (ld_rlx(&p->gen) != mygen) break;          /* generation moved: signalled */
        park(p->park_id);                             /* block until woken */
    }
    pthread_mutex_lock(m);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* c) {
    struct bx_cond* p = (struct bx_cond*)c;
    __atomic_fetch_add(&p->gen, 1, __ATOMIC_ACQ_REL); /* bump before wake */
    unpark(p->park_id, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
    struct bx_cond* p = (struct bx_cond*)c;
    __atomic_fetch_add(&p->gen, 1, __ATOMIC_ACQ_REL);
    unpark(p->park_id, 64);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* c) {
    struct bx_cond* p = (struct bx_cond*)c;
    (void)boruix_sync_delete(p->park_id);
    p->_init = 0;
    return 0;
}

/* ---- semaphore ---- */
int sem_init(bx_sem_t* s, int pshared, unsigned int value) {
    struct bx_sem* p = (struct bx_sem*)s;
    (void)pshared;
    ulong id = boruix_sync_create(0);
    if ((long)id < 0) return -1;
    p->count = (int)value; p->park_id = id; p->_init = 1;
    return 0;
}

int sem_wait(bx_sem_t* s) {
    struct bx_sem* p = (struct bx_sem*)s;
    if (!p->_init) return -1;
    for (;;) {
        int c = ld_rlx(&p->count);
        if (c > 0 && cas_int(&p->count, c, c - 1)) return 0; /* decrement if positive */
        park(p->park_id);                                    /* empty: block */
    }
}

int sem_trywait(bx_sem_t* s) {
    struct bx_sem* p = (struct bx_sem*)s;
    int c = ld_rlx(&p->count);
    if (c > 0 && cas_int(&p->count, c, c - 1)) return 0;
    return -1;
}

int sem_post(bx_sem_t* s) {
    struct bx_sem* p = (struct bx_sem*)s;
    __atomic_fetch_add(&p->count, 1, __ATOMIC_ACQ_REL);
    unpark(p->park_id, 1);
    return 0;
}

int sem_destroy(bx_sem_t* s) {
    struct bx_sem* p = (struct bx_sem*)s;
    (void)boruix_sync_delete(p->park_id);
    p->_init = 0;
    return 0;
}

int sem_getvalue(bx_sem_t* s, int* val) {
    struct bx_sem* p = (struct bx_sem*)s;
    *val = ld_rlx(&p->count);
    return 0;
}
