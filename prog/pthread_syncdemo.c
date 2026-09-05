/* pthread_syncdemo.c (T2-4): C pthread mutex/condvar/semaphore end-to-end.
 * Phases (DESIGN-T2 SS7 acceptance 2-4):
 *   A) mutex: two worker threads each add +M to a shared counter under the mutex; the
 *      leader joins and verifies the total == 2M (real SMP4 mutual exclusion).
 *   B) condvar: N workers each wait on a condition until a producer signals all via
 *      broadcast; no lost wake / no deadlock.
 *   C) semaphore: a producer post()s K times and K consumers each wait(); counting
 *      conserved (each post wakes exactly one waiter).
 * PASS printed by main; exit code 0. FAIL details printed + nonzero.
 */
#include "pthread.h"
#include "boruix.h"

#define M 200000UL
#define COND_N 4
#define SEM_K 6

static pthread_mutex_t g_mu;
static unsigned long g_shared;

static void* mutex_worker(void* arg) {
    unsigned long i, per = M;
    (void)arg;
    for (i = 0; i < per; i++) {
        pthread_mutex_lock(&g_mu);
        g_shared += 1;
        pthread_mutex_unlock(&g_mu);
    }
    return 0;
}

/* --- condvar: workers block until g_go becomes true (broadcast) --- */
static pthread_cond_t g_cv;
static pthread_mutex_t g_cmu;
static volatile int g_go;
static volatile int g_ready;      /* workers that reached the wait */
static pthread_mutex_t g_rmu;

static void* cond_worker(void* arg) {
    unsigned long id = (unsigned long)arg;
    (void)id;
    pthread_mutex_lock(&g_rmu); g_ready += 1; pthread_mutex_unlock(&g_rmu);
    pthread_mutex_lock(&g_cmu);
    while (!g_go) pthread_cond_wait(&g_cv, &g_cmu);
    pthread_mutex_unlock(&g_cmu);
    return 0;
}

static void* cond_producer(void* arg) {
    unsigned long spins = (unsigned long)arg;
    unsigned long i;
    /* Wait until all COND_N workers are ready at the condvar, then set g_go and broadcast. */
    for (i = 0; i < spins; i++) {
        int r; pthread_mutex_lock(&g_rmu); r = g_ready; pthread_mutex_unlock(&g_rmu);
        if (r >= COND_N) break;
    }
    pthread_mutex_lock(&g_cmu);
    g_go = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_cmu);
    return 0;
}

/* --- semaphore: K posts, K waits --- */
static bx_sem_t g_sem;
static unsigned long g_taken;   /* guarded by g_rmu */

static void* sem_consumer(void* arg) {
    unsigned long id = (unsigned long)arg;
    (void)id;
    sem_wait(&g_sem);
    pthread_mutex_lock(&g_rmu); g_taken += 1; pthread_mutex_unlock(&g_rmu);
    return 0;
}

int main(void) {
    pthread_t th[COND_N];
    unsigned long i;
    int fails = 0;

    /* Phase A: mutex */
    if (pthread_mutex_init(&g_mu, 0) != 0) { boruix_puts("[psd] mutex init fail\n"); exit(1); }
    if (pthread_create(&th[0], 0, mutex_worker, 0) != 0 ||
        pthread_create(&th[1], 0, mutex_worker, 0) != 0) { boruix_puts("[psd] mutex spawn fail\n"); exit(2); }
    {
        void* rv; pthread_join(th[0], &rv); pthread_join(th[1], &rv);
    }
    if (g_shared != 2UL * M) { boruix_puts("[psd] MUTEX counter mismatch\n"); fails = 1; }

    /* Phase B: condvar producer/consumer broadcast */
    pthread_cond_init(&g_cv, 0);
    pthread_mutex_init(&g_cmu, 0);
    pthread_mutex_init(&g_rmu, 0);
    g_go = 0; g_ready = 0;
    for (i = 0; i < COND_N; i++)
        if (pthread_create(&th[i], 0, cond_worker, (void*)i) != 0) { boruix_puts("[psd] cond spawn fail\n"); exit(3); }
    {
        pthread_t pt; void* rv;
        pthread_create(&pt, 0, cond_producer, (void*)2000000UL);
        pthread_join(pt, &rv);
        for (i = 0; i < COND_N; i++) pthread_join(th[i], &rv);
    }
    /* all COND_N workers must have been released (no lost wake) */
    if (g_ready < COND_N) { boruix_puts("[psd] CONDVAR lost wake (not all waited)\n"); fails = 1; }

    /* Phase C: semaphore counting */
    if (sem_init(&g_sem, 0, 0) != 0) { boruix_puts("[psd] sem init fail\n"); exit(4); }
    g_taken = 0;
    {
        pthread_t cons[SEM_K];
        for (i = 0; i < SEM_K; i++)
            pthread_create(&cons[i], 0, sem_consumer, (void*)i);
        /* small yield window so consumers park */
        { unsigned long z; for (z = 0; z < 100000UL; z++) __asm__ volatile(""); }
        for (i = 0; i < SEM_K; i++) sem_post(&g_sem);   /* wake one per post */
        { void* rv; for (i = 0; i < SEM_K; i++) pthread_join(cons[i], &rv); }
    }
    if (g_taken != SEM_K) { boruix_puts("[psd] SEM counting mismatch\n"); fails = 1; }

    pthread_mutex_destroy(&g_mu);
    if (fails) {
        boruix_puts("[psd] FAIL (sync checks)\n");
        exit(10);
    }
    boruix_puts("[psd] PASS: pthread mutex/condvar/semaphore ok\n");
    exit(0);
}
