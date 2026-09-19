/* forkdemo.c (ADR-038 U1/D5): 真实 freestanding C 程序验证 POSIX fork() 端到端。
 *
 * ## 为什么需要这个程序（U1）
 *
 * 内核侧 fork 语义已由 kernel::tests::test_task_derive_e2e 覆盖（走完整 syscall 路径），
 * libc 侧 fork() 包装也已落地并编译通过。但两者之间**从未一起运行过**：
 * 内核测试用内核自己构造的 InterruptFrame 调 syscall_entry；libc 的 fork() 则是
 * 真实用户态代码经 int 0x80 / syscall 进入内核。这中间的差距包括：
 *   - libc fork() 的返回值处理（父收 pid、子收 0）是否真的生效；
 *   - 子进程从 fork() **内部**返回时，用户态栈/寄存器是否完好（这是 fork 最
 *     容易出错的地方——子进程必须能继续正常执行，而不是崩在返回点）；
 *   - 父子各自的地址空间是否真正隔离（写时复制对用户态是否透明）；
 *   - waitpid() 能否正确收到子进程退出码。
 * 本程序就是这条整链的证据。它不依赖 Rust libc——经 csrc/crt0.S + crtrt.c 直连
 * 内核 syscall（S35：真实链路，非模拟）。
 *
 * ## 断言（打印 [forkdemo] OK/FAIL 行，全部可在串口日志中证伪）
 *
 *   1. fork() 在父进程返回 > 0（子 pid），在子进程返回 0；
 *   2. 子进程能继续执行用户态代码（不是崩在返回点）——它打印自己的标记；
 *   3. COW 隔离：父子对**同一全局变量**写入后，各自读回自己的值；
 *   4. 子进程 exit(code) 后，父进程 waitpid() 收到该 pid 与退出码。
 */
typedef unsigned long ulong;

long boruix_syscall6(ulong nr, ulong a1, ulong a2, ulong a3, ulong a4, ulong a5, ulong a6);
long boruix_syscall(ulong nr, ulong a1, ulong a2, ulong a3);
/* 与 boruix_syscall6 同 ABI，但**额外保留 r10**（waitpid 用它交付被收尸 pid）。
 * crtrt.c 的 syscall 把 r10 当 a4 输入，故返回时 r10 已被用户值占据——必须由调用点
 * 自己读出内核写在 r10 的输出（同 libsys::invoke_capture_r10 的做法）。 */
static long boruix_syscall_capture_r10(ulong nr, ulong a1, ulong a2, ulong a3, ulong* out_r10) {
    long ret;
    register ulong r10 asm("r10") = 0;
    register ulong r8  asm("r8")  = 0;
    register ulong r9  asm("r9")  = 0;
    __asm__ volatile("int $0x80"
        : "=a"(ret), "+r"(r10)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    *out_r10 = r10;
    return ret;
}
long boruix_write(long fd, const void* buf, ulong len);
long boruix_puts(const char* s);
void exit(int code);

#define STREAM_OFFSET_CURRENT 0xFFFFFFFFFFFFFFFFUL

/* SYS_TASK_DERIVE(0x3A): flags, entry_rsp, entry_rip -> 父收 pid, 子收 0, 失败负 errno。
 * 三参数全 0 = 继承父当前 RIP/RSP（POSIX fork 语义）。见 docs/adr/038。 */
#define SYS_TASK_DERIVE 0x3AUL

/* SYS_TASK_WAIT(0x32)：既用于 yield（target_pid=0, timeout=0），也用于 waitpid。
 * 与 libsys::nr::SYS_TASK_WAIT 同值（ADR-014 编码；S13 双侧镜像，此处是用户态第三份，
 * 故注释指向内核常量以免漂移）。交付协议（libsys/process.rs:179 成文）：
 *   - rax = 退出码；
 *   - r10 = 被收尸子进程的 pid（同步与阻塞两条路径一致）。
 * 故 waitpid 必须用「保留 r10」的 syscall 变体取 pid，而不是只看 rax。 */
#define SYS_TASK_WAIT 0x32UL
/* WAIT_ANY 哨兵：与内核 task::scheduler::WAIT_ANY 同值（usize::MAX）。 */
#define WAIT_ANY (~0UL)

/* 十进制打印（不依赖 stdio，保持零外部依赖）。 */
static void put_dec(long v) {
    char b[24];
    int i = 0;
    unsigned long u;
    if (v < 0) { boruix_write(1, "-", 1); u = (unsigned long)(-v); }
    else { u = (unsigned long)v; }
    if (u == 0) b[i++] = '0';
    while (u) { b[i++] = (char)('0' + (u % 10)); u /= 10; }
    while (i > 0) { boruix_write(1, &b[--i], 1); }
}

static int fails = 0;

static void check(int cond, const char* what) {
    boruix_puts(cond ? "[forkdemo] OK   " : "[forkdemo] FAIL ");
    boruix_puts(what);
    boruix_write(1, "\n", 1);
    if (!cond) fails++;
}

/* COW 隔离的检测对象：**可写全局变量**（在 .data 段，父子共享同一物理帧的 COW 页）。
 * 父子各写不同的值，再各自读回——若 COW 失效（双向写窗口），两边会读到对方的值。 */
static volatile long shared_var = 111;

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    boruix_puts("[forkdemo] === ADR-038 U1: POSIX fork() end-to-end (real C userland) ===\n");

    /* 记录父进程写入前的值与自身 pid，供子进程打印对比。 */
    long parent_pid = (long)boruix_syscall(0x39UL, 0, 0, 0); /* SYS_TASK_GETPID */
    boruix_puts("[forkdemo] parent pid="); put_dec(parent_pid);
    boruix_puts(" shared_var="); put_dec(shared_var);
    boruix_write(1, "\n", 1);

    /* ---- fork：三参数全 0 = 继承父 RIP/RSP ---- */
    long rc = boruix_syscall(SYS_TASK_DERIVE, 0, 0, 0);

    if (rc < 0) {
        boruix_puts("[forkdemo] FAIL fork() returned error "); put_dec(rc);
        boruix_write(1, "\n", 1);
        exit(1);
    }

    if (rc == 0) {
        /* ===== 子进程分支（fork 返回 0） ===== */
        long child_pid = (long)boruix_syscall(0x39UL, 0, 0, 0);
        long child_tid = (long)boruix_syscall(0x38UL, 0, 0, 0); /* SYS_TASK_GETTID */
        boruix_puts("[forkdemo] child: running, pid="); put_dec(child_pid);
        boruix_puts(" tid="); put_dec(child_tid);
        boruix_puts(" (parent was "); put_dec(parent_pid);
        boruix_write(1, ")\n", 2);

        /* 断言 2：子进程能继续执行到这里，本身就是「返回点未损坏」的证据。 */
        check(child_pid > 0, "child has its own pid (new thread group)");
        check(child_tid == child_pid, "child is the group leader (tid == pid, as fork requires)");
        check(parent_pid != child_pid, "child pid differs from parent pid");
        check(shared_var == 111, "child sees parent's pre-fork value 111 (inherit OK)");

        /* 断言 3（子侧）：写全局变量，制造 COW 故障。 */
        shared_var = 222;
        check(shared_var == 222, "child write took effect in child (COW copy on write)");

        boruix_puts("[forkdemo] child: exiting with code 42\n");
        exit(42);
        /* 不会到达 */
    }

    /* ===== 父进程分支（fork 返回子 pid > 0） ===== */
    boruix_puts("[forkdemo] parent: fork() returned child pid="); put_dec(rc);
    boruix_write(1, "\n", 1);
    check(rc > 0, "fork() returned positive pid in parent");
    check(rc != parent_pid, "returned pid is a new process, not the parent");

    /* 断言 3（父侧）：父写自己的值，须与子进程互不影响。 */
    shared_var = 333;
    check(shared_var == 333, "parent write took effect in parent");

    /* 让子进程有机会先跑完（本系统无优先级保证；父可能先返回）。
     * yield = SYS_TASK_WAIT(target_pid=0, timeout=0)。 */
    boruix_syscall(SYS_TASK_WAIT, 0, 0, 0);

    /* 断言 4：waitpid(WAIT_ANY) 收到子进程的 pid 与退出码 42。
     * 交付协议：rax = 退出码，r10 = 子 pid。 */
    ulong got_pid = 0;
    long code = boruix_syscall_capture_r10(SYS_TASK_WAIT, WAIT_ANY, 0, 0, &got_pid);
    boruix_puts("[forkdemo] waitpid -> pid="); put_dec((long)got_pid);
    boruix_puts(" code="); put_dec(code);
    boruix_write(1, "\n", 1);
    check((long)got_pid == rc, "waitpid returned the forked child's pid (via r10)");
    check(code == 42, "child exit code 42 observed via waitpid (via rax)");

    /* 断言 3（父侧收尾）：子进程写过的 shared_var 在父侧**必须**仍是 333。
     * 若 COW 失效，父会看到 222 —— 这正是本程序要抓的静默数据腐蚀。 */
    check(shared_var == 333, "parent value untouched by child write (COW isolation holds)");

    if (fails == 0) {
        boruix_puts("[forkdemo] PASS (fork/COW/waitpid verified through real C userland)\n");
    } else {
        boruix_puts("[forkdemo] FAILURES: "); put_dec(fails);
        boruix_write(1, "\n", 1);
    }
    exit(fails == 0 ? 0 : 1);
    return 0;
}