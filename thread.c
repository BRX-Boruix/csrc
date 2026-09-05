/* thread.c - BORUIX C runtime thread/memory syscall glue (T2-3, threads.md).
 * Direct int 0x80 wrappers for mmap/munmap and the TASK thread syscalls, mirroring
 * libsys ABIs (S13). Each returns <0 = -errno on failure.
 */
#include "boruix.h"

/* SYS_MEMORY_MAP 0x21: a1=size -> addr */
ulong boruix_mmap(ulong size) {
    return (ulong)boruix_syscall(0x21UL, size, 0, 0);
}
/* SYS_MEMORY_UNMAP 0x24: a1=addr a2=size -> 0/-errno */
long boruix_munmap(ulong addr, ulong size) {
    return boruix_syscall(0x24UL, addr, size, 0);
}
/* SYS_TASK_THREAD_SPAWN 0x35: entry, stack_top, starter -> tid */
ulong boruix_thread_spawn_with_starter(ulong entry, ulong stack_top, ulong starter) {
    return boruix_syscall(0x35UL, entry, stack_top, starter);
}
/* SYS_TASK_THREAD_JOIN 0x36: tid -> exit code (leader only). Blocking. */
long boruix_thread_join(ulong tid) {
    return boruix_syscall(0x36UL, tid, 0, 0);
}
/* SYS_TASK_EXIT 0x34: never returns. Leader call exits whole process; member call
 * exits only that member (leaves joinable zombie). */
ulong boruix_thread_exit(int code) {
    boruix_syscall(0x34UL, (ulong)(unsigned)code, 0, 0);
    for (;;) {}
}
/* SYS_TASK_GETTID 0x38: own pid / thread id. */
ulong boruix_gettid(void) {
    return boruix_syscall(0x38UL, 0, 0, 0);
}

/* SYS_SYNC_CREATE 0x71: a1=init_value -> sync_id (futex-style kernel sync word) */
ulong boruix_sync_create(ulong init_value) {
    return boruix_syscall(0x71UL, init_value, 0, 0);
}
/* SYS_SYNC_WAIT 0x72: a1=id a2=expected a3=timeout_ns(0=forever). Blocks while value==expected;
 * returns current value (may spin WouldBlock? mirror callers). Value must be bit63-clear. */
ulong boruix_sync_wait(ulong id, ulong expected, ulong timeout_ns) {
    return boruix_syscall(0x72UL, id, expected, timeout_ns);
}
/* SYS_SYNC_WAKE 0x73: a1=id a2=value a3=n -> actually woken count */
ulong boruix_sync_wake(ulong id, ulong value, ulong n) {
    return boruix_syscall(0x73UL, id, value, n);
}
/* SYS_SYNC_DELETE 0x74: a1=id -> 0 or Busy if waiters remain */
long boruix_sync_delete(ulong id) {
    return boruix_syscall(0x74UL, id, 0, 0);
}
/* SYS_TASK_GETPID 0x39: group leader pid / tgid. */
ulong boruix_getpid(void) {
    return boruix_syscall(0x39UL, 0, 0, 0);
}
