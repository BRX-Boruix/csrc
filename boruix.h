/* boruix.h - BORUIX freestanding C runtime public declarations (T2-0 substrate).
 * Direct kernel syscalls via int 0x80; no Rust libc. Shared by crtrt.c, thread.c,
 * pthread.c and all C programs. Error convention: syscall helpers return <0 = -errno.
 */
#ifndef BORUIX_H
#define BORUIX_H

typedef unsigned long ulong;

/* ---- generic syscall ---- */
long boruix_syscall6(ulong nr, ulong a1, ulong a2, ulong a3, ulong a4, ulong a5, ulong a6);
long boruix_syscall(ulong nr, ulong a1, ulong a2, ulong a3);

/* ---- basic stdio/exit (crtrt.c) ---- */
void exit(int code);
long boruix_write(long fd, const void* buf, ulong len);
long boruix_puts(const char* s);

/* ---- memory (thread.c): mmap/munmap ---- */
ulong boruix_mmap(ulong size);          /* SYS_MEMORY_MAP 0x21, returns addr or -errno */
long  boruix_munmap(ulong addr, ulong size); /* SYS_MEMORY_UNMAP 0x24, 0 or -errno */

/* ---- task/thread (thread.c) ---- */
ulong boruix_thread_spawn_with_starter(ulong entry, ulong stack_top, ulong starter); /* 0x35 */
long  boruix_thread_join(ulong tid);    /* 0x36: returns exit code (leader reap) or -errno */
ulong boruix_thread_exit(int code);     /* 0x34: never returns */
ulong boruix_gettid(void);              /* 0x38: own pid/thread id */
ulong boruix_getpid(void);              /* 0x39: group leader pid / tgid */

/* ---- sync (thread.c): kernel futex-style sync words (ADR-032) ---- */
ulong boruix_sync_create(ulong init_value);    /* 0x71 -> sync_id */
ulong boruix_sync_wait(ulong id, ulong expected, ulong timeout_ns); /* 0x72 -> cur value */
ulong boruix_sync_wake(ulong id, ulong value, ulong n);            /* 0x73 -> woken */
long  boruix_sync_delete(ulong id);            /* 0x74 -> 0/Busy */

/* ---- minimal stdio (stdio.c, freestanding, %s %c %d %u %x %%) ---- */
int bx_printf(const char* fmt, ...);
int bx_snprintf(char* out, unsigned long cap, const char* fmt, ...);

/* ---- error names ---- */
#define BX_ENOTSUP (-95L)
#define BX_ENOENT  (-2L)
#define BX_EINVAL  (-22L)
#define BX_EWOULDBLOCK (-11L)
#define BX_ESRCH   (-3L)
#define BX_ENOMEM  (-12L)

#endif
