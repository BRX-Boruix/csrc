/* pthread.c - BORUIX freestanding C pthread lifecycle (T2-3, threads.md).
 * Implements pthread_create/join/detach/self/exit over the kernel thread syscalls,
 * using a per-thread descriptor in a fixed registry (threads share the group addr space,
 * so a registry array in .bss is visible to all group threads).
 *
 * Entry model: pthread_create registers a descriptor slot and calls
 * boruix_thread_spawn_with_starter(pthread_thread_entry, stack_top, &slot). On first
 * run the kernel sets rdi = starter = &slot. pthread_thread_entry (naked) aligns the
 * stack and jumps to pthread_thread_body(slot), which runs start_routine(arg), stores
 * the return value into slot->retval, marks EXITED, then boruix_thread_exit(0).
 * The leader's pthread_join first does the kernel thread_join(tid) (returns the integer
 * exit code) and then reads slot->retval (the void*), because the kernel join carries
 * only an integer. pthread_self/pthread_exit find their slot by gettid().
 */
#include "boruix.h"
#include "pthread.h"

#define PTH_MAX 32
#define PTH_STACK_SIZE 0x20000UL

struct pthread_desc {
    ulong tid;
    int   state;
    int   joined;
    ulong stack_base;
    ulong stack_size;
    void* retval;
    void* (*start)(void*);
    void* arg;
    /* debug name char slot */
};

static struct pthread_desc g_threads[PTH_MAX];
static int g_main_registered = 0;

static struct pthread_desc* desc_by_tid(ulong tid) {
    int i;
    for (i = 0; i < PTH_MAX; i++) {
        if (g_threads[i].tid == tid && g_threads[i].state != 0) return &g_threads[i];
    }
    return 0;
}

/* Register the leader (main) so pthread_self works in main. Called lazily. */
static void ensure_main(void) {
    ulong mytid;
    if (g_main_registered) return;
    mytid = boruix_gettid();
    g_threads[0].tid = mytid;
    g_threads[0].state = PTH_ST_RUNNING;
    g_threads[0].retval = 0;
    g_threads[0].joined = 0;
    g_main_registered = 1;
}

/* Full memory fence: orders the retval store (publishing thread) vs the read back in
 * the joiner. x86-64 is TSO but the publisher and joiner run on different cores; the
 * kernel join establishes the happens-before via its scheduler locking, and these fences
 * pin the user-side accesses around the syscall so the retval handoff is not lost to
 * store-buffer/coherence timing. (Without them a single join could return before the
 * publisher's plain store to slot->retval is globally visible.) */
static void mfence(void) { __asm__ volatile("mfence" ::: "memory"); }

void pthread_thread_body(struct pthread_desc* slot) {
    /* Self-identify first: the leader writes slot->tid only after the spawn syscall
     * returns, but this thread may start (and call pthread_self -> desc_by_tid) before
     * that write is visible on this core. Setting our own tid here (== gettid, same
     * value the leader assigns) makes pthread_self find this slot race-free. */
    slot->tid = boruix_gettid();
    void* rv = slot->start(slot->arg);
    slot->retval = rv;
    mfence();          /* release: retval store visible before we exit */
    slot->state = PTH_ST_EXITED;
    boruix_thread_exit(0); /* never returns */
}

/* Naked entry: kernel sets rdi = &slot (starter); align stack; jump to body. */
__attribute__((naked)) static void pthread_thread_entry(void) {
    __asm__ volatile(
        ".intel_syntax noprefix\n\t"
        "and rsp, -16\n\t"
        "jmp pthread_thread_body\n\t"
    );
}

int pthread_create(pthread_t* t, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg) {
    struct pthread_desc* slot;
    int i;
    ulong stack, tid;
    (void)attr;
    ensure_main();
    slot = 0;
    for (i = 0; i < PTH_MAX; i++) {
        if (g_threads[i].state == 0) { slot = &g_threads[i]; break; }
    }
    if (!slot) return -1; /* EAGAIN: registry full */
    stack = boruix_mmap(PTH_STACK_SIZE);
    if ((long)stack < 0) return -1;
    slot->tid = 0;
    slot->state = PTH_ST_RUNNING;
    slot->joined = 0;
    slot->stack_base = stack;
    slot->stack_size = PTH_STACK_SIZE;
    slot->retval = 0;
    slot->start = start_routine;
    slot->arg = arg;
    /* stack top = base + size (kernel iretq sets rsp=stack_top, stack grows down). */
    tid = boruix_thread_spawn_with_starter((ulong)&pthread_thread_entry,
                                           stack + PTH_STACK_SIZE, (ulong)slot);
    if ((long)tid < 0) {
        slot->state = 0;
        return -1;
    }
    slot->tid = tid;
    *t = (pthread_t)slot;
    return 0;
}

int pthread_join(pthread_t t, void** retval) {
    struct pthread_desc* slot = (struct pthread_desc*)t;
    long code;
    ulong mytid;
    int tries = 0;
    if (!slot || slot->state == 0) return -1; /* ESRCH */
    if (slot->joined) return -1;              /* ESRCH: already reaped */
    mytid = boruix_gettid();
    if (slot->tid == mytid) return -1;        /* EDEADLK: joining self */
    if (slot->state == PTH_ST_DETACHED) return -1;
    /* Kernel thread_join blocks the leader; on SMP it may report WouldBlock when it
     * cannot block right now -> retry with a yield (mirror threaddemo join). */
    for (;;) {
        code = boruix_thread_join(slot->tid);
        if (code >= 0) break;
        if (code == BX_EWOULDBLOCK) {
            tries++;
            if (tries > 2000000) return -1;
            boruix_syscall(0x32UL, 0, 0, 0); /* yield */
            continue;
        }
        return (int)code; /* NotFound etc. */
    }
    mfence(); /* acquire: only read slot->retval after the kernel join reaped the writer */
    if (retval) *retval = slot->retval;
    slot->joined = 1;
    slot->state = 0; /* slot reusable */
    return 0;
}

int pthread_detach(pthread_t t) {
    struct pthread_desc* slot = (struct pthread_desc*)t;
    if (!slot || slot->state == 0) return -1; /* ESRCH */
    slot->state = PTH_ST_DETACHED;
    /* Real reap happens via group exit / leader lazy collection (see design doc). */
    return 0;
}

pthread_t pthread_self(void) {
    struct pthread_desc* slot;
    ensure_main();
    slot = desc_by_tid(boruix_gettid());
    return (pthread_t)slot;
}

void pthread_exit(void* retval) {
    struct pthread_desc* slot = desc_by_tid(boruix_gettid());
    if (slot) {
        slot->retval = retval;
        mfence(); /* release: retval store visible before exit */
        slot->state = PTH_ST_EXITED;
    }
    boruix_thread_exit(0); /* never returns */
}

int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}
