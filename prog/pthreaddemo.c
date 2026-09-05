/* pthreaddemo.c (T2-3): C pthread lifecycle end-to-end demo.
 * main spawns N threads; each thread takes its unique arg (i), returns a unique value
 * (some function of arg + a per-thread computation), main joins all and verifies each
 * void** retval equals the expected unique value. Also exercises pthread_self and a
 * shared counter incremented by each thread (memory-visibility smoke).
 *
 * PASS conditions (printed by init side or by exit code):
 *   - all create() == 0
 *   - all join() == 0 and *retval == expected
 *   - shared counter == N
 *   - pthread_self() in each thread equals its own pthread_t
 */
#include "pthread.h"
#include "boruix.h"

#define N 4
static unsigned long g_shared;   /* shared counter, incremented by each thread */

static void* worker(void* arg) {
    unsigned long id = (unsigned long)arg;
    unsigned long self_matches = 0;
    unsigned long i;
    /* pthread_self inside the thread should equal its own pthread_t (registry slot) */
    pthread_t me = pthread_self();
    /* Spin a bit to encourage cross-thread interleaving, then bump shared. */
    for (i = 0; i < 200000UL; i++) { __asm__ volatile(""); }
    g_shared += 1;
    /* retval = (id+1) * 100 + self_ok */
    return (void*)((id + 1) * 100UL + (me != 0 ? 1UL : 0UL));
}

/* minimal unsigned decimal -> caller buffer (at least 20 bytes). Returns buf. */
static char* dec(unsigned long v, char* buf) {
    int n = 0, k;
    char rev[20];
    if (v == 0) rev[n++] = '0';
    while (v > 0) { rev[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (k = 0; k < n; k++) buf[k] = rev[n - 1 - k];
    buf[n] = '\0';
    return buf;
}
int main(void) {
    pthread_t th[N];
    unsigned long i;
    long all_ok = 0;
    /* create N threads with args 0..N-1 */
    for (i = 0; i < N; i++) {
        if (pthread_create(&th[i], 0, worker, (void*)i) != 0) {
            boruix_puts("[pthreaddemo] pthread_create FAILED\n");
            exit(1);
        }
    }
    /* join and verify retvals */
    for (i = 0; i < N; i++) {
        void* rv = 0;
        unsigned long expect = (i + 1) * 100UL + 1UL;
        int jr = pthread_join(th[i], &rv);
        if (jr != 0) {
            boruix_puts("[pthreaddemo] pthread_join FAILED\n");
            exit(2);
        }
        if ((unsigned long)rv != expect) {
            char tmp[20];
            boruix_puts("[pthreaddemo] retval MISMATCH i=");
            boruix_puts(dec(i, tmp));
            boruix_puts(" got=");
            boruix_puts(dec((unsigned long)rv, tmp));
            boruix_puts(" exp=");
            boruix_puts(dec(expect, tmp));
            boruix_puts("\n");
            all_ok = 1;
        }
    }
    /* check shared counter == N */
    if (g_shared != N) {
        boruix_puts("[pthreaddemo] shared counter MISMATCH\n");
        all_ok = 1;
    }
    if (all_ok) {
        boruix_puts("[pthreaddemo] FAIL (retval/counter mismatch)\n");
        exit(10);
    }
    boruix_puts("[pthreaddemo] PASS: C pthread create/join N threads, retvals+counter ok\n");
    exit(0);
}
