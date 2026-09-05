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
/* SYS_TASK_GETPID 0x39: group leader pid / tgid. */
ulong boruix_getpid(void) {
    return boruix_syscall(0x39UL, 0, 0, 0);
}
