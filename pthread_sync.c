/* pthread_sync.c - BORUIX freestanding C pthread mutex/condvar/semaphore (T2-4).
 * Implements pthread_mutex_*, pthread_cond_*, sem_* over user-space atomics + the kernel
 * SYNC word (ADR-032) as the park/unpark primitive (DESIGN-T2 SS5, zero kernel changes).
 * Correctness core (T2-5 deadlock fix): the SYNC word value is a lost-wakeup guard.
 * sync_wait(id,expected,0) blocks only while word==expected and re-checks at registration.
 * If a waker CHANGES the word value before a waiter finishes its decide-to-park sequence,
 * sync_wait returns immediately instead of sleeping. The old value-0 park (word forever 0)
 * defeated this: a wake in the decide-to-park window was silently lost and the waiter slept
 * forever on a satisfied condition. So every object parks with expected = a monotonic
 * epoch/gen it snapshots, and the waker bumps that epoch into the word BEFORE waking.
 *
 * Mutex: atomic state 0=unlocked,1=locked(no waiter),2=locked(may have waiters) + epoch.
 *   lock  : CAS 0->1 fast; else register waiter flag (->2) then park expected=epoch.
 *   unlock: exchange->0; if old==2 bump epoch into word then wake one.
 * Condvar: user gen counter mirrored to the park word. waiter snapshots gen under the
 *   mutex, unlocks, then parks expected=gen (a single park: any signal >= gen returns).
 *   signal bumps gen into the word then wakes one; broadcast bumps gen then wakes all.
 * Sem: atomic count + epoch. wait: try dec(>0) else park expected=epoch; post bumps epoch
 *   into the word then wakes one. All epoch bumps precede the wake: no lost wakeup.
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

/* ---- mutex ---- */
int pthread_mutex_init(pthread_mutex_t* m, const void* attr) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    (void)attr;
    ulong id = boruix_sync_create(0);
    if ((long)id < 0) return -1;
    p->state = 0; p->epoch = 0; p->park_id = id; p->_init = 1;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* m) {
    struct bx_mutex* p = (struct bx_mutex*)m;
    if (!p->_init) return -1;
    if (cas_int(&p->state, 0, 1)) return 0;            /* fast: uncontended */
    for (;;) {
        int s = ld_rlx(&p->state);
        if (s == 0) { if (cas_int(&p->state, 0, 1)) return 0; continue; } /* freed: grab */
        if (s == 1 && !cas_int(&p->state, 1, 2)) continue; /* promote to with-waiter */
        /* state == 2 now: we are a registered potential waiter. Park with expected = the
         * epoch we snapshot. The unlock bumps epoch (and the SYNC word value) BEFORE waking,
         * so a release that lands in our decide-to-park window makes the kernel re-check
         * (word != expected) return immediately instead of sleeping on a free mutex. */
        unsigned long e = ld_rlx(&p->epoch);
        if (ld_rlx(&p->state) != 2) continue;          /* freed during setup: retry */
        (void)boruix_sync_wait(p->park_id, e, 0);      /* block unless epoch advanced */
        if (cas_int(&p->state, 0, 2)) return 0;        /* acquired (keep waiters flag) */
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
    if (old == 2) {
        unsigned long e = __atomic_add_fetch(&p->epoch, 1, __ATOMIC_ACQ_REL);
        (void)boruix_sync_wake(p->park_id, e, 1);      /* word = new epoch, wake one */
    }
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
    /* The kernel SYNC word value mirrors the condvar generation. Parking with expected =
     * mygen lets the kernel re-check (word == mygen?) at registration: a signal that bumps
     * the word past mygen in the check-then-park window makes sync_wait return immediately
     * (non-blocking) instead of sleeping forever. This closes the lost-wakeup window the
     * committed constant-zero-park design left open (S09: word value is the signal counter).
     * Returns on ANY signal >= mygen; the caller re-checks its predicate (spurious-safe). */
    (void)boruix_sync_wait(p->park_id, (ulong)mygen, 0);
    pthread_mutex_lock(m);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* c) {
    struct bx_cond* p = (struct bx_cond*)c;
    int ng = __atomic_add_fetch(&p->gen, 1, __ATOMIC_ACQ_REL); /* bump; ng = new gen */
    (void)boruix_sync_wake(p->park_id, (ulong)ng, 1);          /* word = ng, wake one */
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
    struct bx_cond* p = (struct bx_cond*)c;
    int ng = __atomic_add_fetch(&p->gen, 1, __ATOMIC_ACQ_REL);
    (void)boruix_sync_wake(p->park_id, (ulong)ng, 64);         /* word = ng, wake all */
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
    p->count = (int)value; p->epoch = 0; p->park_id = id; p->_init = 1;
    return 0;
}

int sem_wait(bx_sem_t* s) {
    struct bx_sem* p = (struct bx_sem*)s;
    if (!p->_init) return -1;
    for (;;) {
        int c = ld_rlx(&p->count);
        if (c > 0 && cas_int(&p->count, c, c - 1)) return 0; /* decrement if positive */
        unsigned long e = ld_rlx(&p->epoch);                 /* snapshot before deciding */
        if (ld_rlx(&p->count) > 0) continue;                 /* a post arrived: retry */
        /* A post in the decide-to-park window bumps epoch+SYNC word -> kernel re-check
         * returns immediately (word != expected) so we never sleep on an available count. */
        (void)boruix_sync_wait(p->park_id, e, 0);            /* block unless a post occurred */
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
    unsigned long e = __atomic_add_fetch(&p->epoch, 1, __ATOMIC_ACQ_REL);
    (void)boruix_sync_wake(p->park_id, e, 1);              /* word = new epoch, wake one */
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
