# csrc

**简体中文** | [English](#english)

BORUIX 的 **freestanding C 运行环境**——让普通 C 程序能在这个系统上编译、运行的一小组运行时源码。

这个仓库提供的是 C 程序运行所需的**最小底座**：程序入口、系统调用封装、退出处理、线程与同步
原语、极简格式化输出。它**不依赖任何外部 C 库**，直接通过系统调用与内核通信。

---

## 它解决什么问题

BORUIX 的绝大多数程序用 Rust 写。但要让 C 程序也能运行，需要一套东西：

- 一段启动代码，把控制权从系统入口交到 `main`
- 把 C 函数调用翻译成系统调用的胶水层
- `exit` 之类的进程收尾处理
- 线程创建/汇合、互斥锁、条件变量、信号量

`csrc` 就是这些。它刻意**不实现标准 C 库**——没有 `malloc`、没有完整 `stdio`、没有文件流——
只提供让 C 程序能跑起来并与内核对话所必需的部分。

## 内容

| 文件 | 内容 |
| --- | --- |
| `crt0.S` | 程序入口 `_start`，设置好环境后跳转到 `main` |
| `crtrt.c` | 系统调用封装、`exit`、字符串输出 |
| `thread.c` | 内存映射、线程创建/汇合/退出、内核同步字 |
| `pthread.c` / `pthread.h` | pthread 接口：线程生命周期 |
| `pthread_sync.c` | 互斥锁、条件变量、信号量 |
| `stdio.c` | 极简格式化输出（`%s %c %d %u %x`） |
| `boruix.h` | 公开声明，供所有 C 程序包含 |
| `linker.ld` | 用户态程序段布局 |
| `build_c.py` | 把单个 C 程序编译链接成一个可执行文件 |
| `prog/` | 示例程序 |

## 系统调用约定

所有系统调用经 `int 0x80` 发出，寄存器分配与内核约定一致。错误以**负数 errno** 返回，调用方
按 `< 0` 判错——这与 C 标准库的 `errno` 全局变量不同，是有意的简化。

## 线程与同步

提供的是一个 **POSIX 风格的子集**，不是完整的 pthread 实现。它的设计受内核能力约束，有几处
诚实的边界值得说明：

- **`pthread_t` 是一个不透明指针**，指向运行时的线程描述符。而 `pthread_join` 只能取出
  **整数退出码**——所以线程的返回值经由描述符传递，而不是由内核携带。
- **`pthread_join` 只能由组长线程调用**。这是内核回收机制的限制，不是接口设计偏好。
- **`pthread_attr_t` 目前被忽略**——创建线程时不支持自定义属性。
- 同步对象（互斥锁、条件变量、信号量）是**完整类型**而非不透明类型，这样程序可以把它们声明
  为全局变量或栈变量。每个对象内部同时持有用户态状态与内核同步字，阻塞与唤醒由内核完成。

线程的返回值传递、共享计数器、`pthread_self` 的自洽性，都由示例程序端到端验证。

## 示例程序

`prog/` 下的程序既是功能验证，也是用法示例：

| 程序 | 演示内容 |
| --- | --- |
| `chelldemo.c` | 最小的 C 程序：入口、参数、输出、退出 |
| `pthreaddemo.c` | 创建 N 个线程、各自返回唯一值、主线程汇合校验 |
| `pthread_syncdemo.c` | 互斥锁 / 条件变量 / 信号量 |
| `pthread_bench.c` | 递归创建与汇合的基准测试 |
| `forkdemo.c` | `fork()` 与 `waitpid()` |

## 构建

```bash
python build_c.py <程序名> <源码目录> <输出目录> [额外的运行时 .c 文件...]
```

例如：

```bash
python build_c.py chelldemo prog _build
```

脚本会编译启动代码、运行时与目标程序，然后链接成一个可执行文件。

需要 **clang**（用作交叉编译器，目标平台 `x86_64-unknown-none`）与 **LLD** 链接器。脚本在
运行时探测可用的 LLD，并把最终选择的路径打印出来，因此在链接器位于非默认位置的环境里同样可用。

## 与 Rust 侧的关系

这个 C 运行环境与 Rust 侧的系统调用封装**相互独立**：C 程序走自己的启动代码，不经过 Rust 的
入口体系。两者直接与同一个内核接口对话。

## 许可

MIT License，版权归 Yang Borui 所有。详见 [LICENSE](LICENSE)。

---

# English

[简体中文](#csrc) | **English**

BORUIX's **freestanding C runtime** — a small set of runtime sources that let ordinary C programs
compile and run on this system.

What this repository provides is the **minimum substrate** a C program needs: an entry point, syscall
glue, exit handling, threading and synchronisation primitives, and minimal formatted output. It
depends on **no external C library** and talks to the kernel through system calls directly.

---

## The problem it solves

Nearly all BORUIX programs are written in Rust. Making C programs run as well requires a set of
pieces:

- a startup stub that hands control from the system entry point to `main`
- glue translating C function calls into system calls
- process teardown such as `exit`
- thread creation/joining, mutexes, condition variables, and semaphores

That is what `csrc` is. It deliberately does **not** implement the standard C library — no
`malloc`, no full `stdio`, no file streams — only what a C program needs to start and converse
with the kernel.

## Contents

| File | Contents |
| --- | --- |
| `crt0.S` | The entry point `_start`, which sets up and jumps to `main` |
| `crtrt.c` | Syscall glue, `exit`, string output |
| `thread.c` | Memory mapping, thread create/join/exit, kernel sync words |
| `pthread.c` / `pthread.h` | The pthread interface: thread lifecycle |
| `pthread_sync.c` | Mutexes, condition variables, semaphores |
| `stdio.c` | Minimal formatted output (`%s %c %d %u %x`) |
| `boruix.h` | Public declarations, included by every C program |
| `linker.ld` | User-space section layout |
| `build_c.py` | Compiles and links a single C program into an executable |
| `prog/` | Example programs |

## Syscall convention

Every system call goes through `int 0x80` with the register assignment the kernel expects. Errors
are returned as **negative errno values**, and callers test `< 0` — deliberately unlike the C
standard library's global `errno` variable.

## Threads and synchronisation

What is provided is a **POSIX-flavoured subset**, not a complete pthread implementation. Its design
is constrained by kernel capabilities, and several honest boundaries are worth stating:

- **`pthread_t` is an opaque pointer** to a runtime thread descriptor. Because `pthread_join` can
  only retrieve an **integer exit code**, a thread's return value travels through the descriptor
  rather than being carried by the kernel.
- **`pthread_join` may only be called from the group leader.** This is a limit of the kernel's
  reaping mechanism, not a design preference.
- **`pthread_attr_t` is currently ignored** — thread creation does not support custom attributes.
- The synchronisation objects (mutex, condition variable, semaphore) are **complete types** rather
  than opaque ones, so programs can declare them as globals or stack variables. Each object holds
  both user-space state and a kernel sync word, with blocking and waking done by the kernel.

Thread return values, the shared counter, and the self-consistency of `pthread_self` are all
verified end-to-end by the example programs.

## Example programs

The programs under `prog/` are both functional verification and usage examples:

| Program | Demonstrates |
| --- | --- |
| `chelldemo.c` | The smallest C program: entry, arguments, output, exit |
| `pthreaddemo.c` | Creating N threads, each returning a unique value, joined and checked by main |
| `pthread_syncdemo.c` | Mutexes, condition variables, semaphores |
| `pthread_bench.c` | A recursive create-and-join benchmark |
| `forkdemo.c` | `fork()` and `waitpid()` |

## Building

```bash
python build_c.py <prog_name> <prog_src_dir> <out_dir> [extra_runtime_c_files...]
```

For example:

```bash
python build_c.py chelldemo prog _build
```

The script compiles the startup stub, the runtime, and the target program, then links them into a
single executable. It requires clang and an LLD linker.

It requires **clang** (used as a cross-compiler targeting `x86_64-unknown-none`) and an **LLD**
linker. The script probes for a usable LLD at run time and prints the path it settled on, so it also
works in environments where the linker lives somewhere non-standard.

## Relationship to the Rust side

This C runtime and the Rust-side syscall wrapper are **independent of each other**: C programs use
their own startup stub and do not go through the Rust entry-point system. Both talk directly to the
same kernel interface.

## License

MIT License, copyright Yang Borui. See [LICENSE](LICENSE).
