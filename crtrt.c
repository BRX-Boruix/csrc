/* BORUIX 用户态 C 运行时 (csrc/crtrt.c) -- 直连内核 syscall, 零外部依赖 (S35).
 * 与 Rust libsys 的 _start/user_main 体系相互独立: C 程序经 crt0.S(_start) -> main,
 * 本文件提供 main 之后进程收尾所需 exit 等 (经 int 0x80)。供 T2-0..T2-5 的 C 程序共用。
 */
typedef unsigned long ulong;

/* int 0x80 系统调用: rax=nr, rdi/rsi/rdx/r10/r8/r9=a1..a6 (与 libsys::invoke 同 ABI)。
 * a4..a6 必须落在 r10/r8/r9: GCC 普通 "r" 约束不会选这些寄存器, 故用局部寄存器变量
 * 显式钉住 (Linux 风格), 否则内核从保存帧读 r10/r8/r9 得垃圾。错误经 bit63 置位返回。 */
long boruix_syscall6(ulong nr, ulong a1, ulong a2, ulong a3, ulong a4, ulong a5, ulong a6) {
    long ret;
    register ulong r10 asm("r10") = a4;
    register ulong r8  asm("r8")  = a5;
    register ulong r9  asm("r9")  = a6;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* 三参数便捷; bit63 置位=错误: 返回负 errno, 调用方按 <0 判错。 */
long boruix_syscall(ulong nr, ulong a1, ulong a2, ulong a3) {
    return boruix_syscall6(nr, a1, a2, a3, 0, 0, 0);
}

/* SYS_TASK_EXIT(0x34): 终止当前进程, 永不返回 (内核切换走, 不回用户态)。 */
void exit(int code) {
    boruix_syscall(0x34UL, (ulong)(unsigned)code, 0, 0);
    for (;;) {}
}

/* STREAM_OFFSET_CURRENT: 顺序(非定位)读/写哨兵。内核 sys_write 对非顺序写拒 ESPIPE,
 * 故顺序写必须在 a4 传此哨兵 (u64::MAX)。与 libsys 的 write 同 ABI。 */
#define STREAM_OFFSET_CURRENT 0xFFFFFFFFFFFFFFFFUL

/* SYS_STREAM_WRITE(0x13): fd, buf, len (a4=顺序哨兵) -> 字节数或负 errno。 */
long boruix_write(long fd, const void* buf, ulong len) {
    return boruix_syscall6(0x13UL, (ulong)fd, (ulong)buf, len, STREAM_OFFSET_CURRENT, 0, 0);
}

/* 写字符串到 fd=1 (自计长), 忽略错误。返回写字节数。 */
long boruix_puts(const char* s) {
    ulong n = 0;
    while (s[n]) n++;
    return boruix_write(1, s, n);
}
