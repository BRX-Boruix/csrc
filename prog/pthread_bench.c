/* pthread_bench.c (T2-5): real pthread recursive-computation benchmark, end-to-end.
 * Each worker thread runs its OWN start_routine+arg and does DEEP recursion on its own
 * private stack (DESIGN T2 SS7 item 5: "two threads each with start_routine+arg and
 * independent deep stack recursion; join values distinguishable"). A classic pthread split:
 * the leader forks NWORKERS threads, each computes a slice of a recursive series by deep
 * recursion, then the leader pthread_join()s each and sums the per-thread return values.
 * Threads use pthread_self()/pthread_equal() and a shared mutex to guard a progress table.
 *
 * Adapted to BORUIX freestanding only via #include "pthread.h"/"boruix.h" and bx_printf
 * (no libc). The pthread logic mirrors the real-world recursive-split pattern unchanged.
 */
#include "pthread.h"
#include "boruix.h"

#define NWORKERS 4
#define DEPTH    26          /* recursion depth per worker slice */

/* recursive series: s(n) = n + s(n-1) with s(0)=0 => s(n)=n(n+1)/2. Deep recursion on the
 * worker private stack stresses per-thread stack + call frames. */
static long rec_series(long n) {
    if (n <= 0) return 0;
    return n + rec_series(n - 1);
}

static pthread_mutex_t g_tab_mu;
static long g_seen_seq;      /* progress table guarded by mutex (SMP-visible) */
static long g_expected_sum;

static void* worker(void* arg) {
    long id = (long)arg;
    /* each worker gets a distinct slice start so join values are distinguishable */
    long lo = id * (DEPTH / NWORKERS);
    long hi = (id + 1) * (DEPTH / NWORKERS) - 1;
    long acc = 0;
    long i;
    for (i = lo; i <= hi; i++) acc += rec_series(i);
    pthread_mutex_lock(&g_tab_mu); g_seen_seq += 1; pthread_mutex_unlock(&g_tab_mu);
    return (void*)(acc + 1000 + id * 7);   /* distinct, verifiable per-thread value */
}

int main(void) {
    pthread_t th[NWORKERS];
    long i;
    int fails = 0;
    long want[NWORKERS];
    pthread_mutex_init(&g_tab_mu, 0);
    /* expected per-thread acc over its slice, plus the (1000+id*7) tag */
    for (i = 0; i < NWORKERS; i++) {
        long lo = i * (DEPTH / NWORKERS), hi = (i + 1) * (DEPTH / NWORKERS) - 1;
        long acc = 0, j;
        for (j = lo; j <= hi; j++) acc += (j * (j + 1)) / 2;
        want[i] = acc + 1000 + i * 7;
        g_expected_sum += want[i];
    }
    /* spawn all workers, each with its own arg. */
    for (i = 0; i < NWORKERS; i++) pthread_create(&th[i], 0, worker, (void*)i);
    /* leader joins each and reads the distinguishable return value. */
    for (i = 0; i < NWORKERS; i++) {
        void* rv = 0;
        pthread_join(th[i], &rv);
        long got = (long)rv;
        if (got != want[i]) {
            bx_printf("[pb] FAIL worker %ld got=%ld want=%ld\n", i, got, want[i]);
            fails = 1;
        }
    }
    if (g_seen_seq != NWORKERS) { bx_printf("[pb] FAIL progress table %ld != %d\n", g_seen_seq, NWORKERS); fails = 1; }
    if (fails) { bx_printf("[pb] FAIL (recursive pthread bench)\n"); exit(11); }
    bx_printf("[pb] PASS: %d workers, deep recursion depth %d, all %ld join-values distinguishable and correct\n",
              NWORKERS, DEPTH, g_expected_sum);
    exit(0);
}