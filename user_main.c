/* user_main.c - 系统入口约定到 C 约定的桥接（3P1-2 sysroot 运行时对象之一）。
 *
 * 系统侧入口 `_start` 由 libsys 提供：它按 ABI 传 (argc, argv)、调用 `user_main`，
 * 返回值即 exit code。C 程序的入口叫 `main`——本对象把两者接起来，使第三方 C 程序
 * 可以按标准写法（#include <stdio.h> + int main）链接 sysroot 的 lib/libc.a。
 *
 * 为什么不复用 crt0.S：crt0.S 自带一个**强** `_start`，与 libc.a（经 libsys）里的入口
 * 互斥——实测 `ld.lld: error: duplicate symbol: _start`。而 libsys 的 `_start` 已经做完
 * 了同样的工作（对齐栈、传参、以返回值为 exit code），故此处只补缺失的那一段。
 *
 * 注意：本文件**不是** freestanding 运行时（crtrt.c 那一族）的一部分——它依赖 libc 提供
 * 的 `main` 符号，只用于 sysroot 的 C 链接配方。
 */
extern int main(int argc, char **argv);

/* 注册入口参数（定义在 libc，声明见 libc/include/boruix.h）。
 *
 * 此处**重复声明而不 include**：本文件是 sysroot 的**链接配方**之一，编译时不依赖 sysroot
 * 的 include 路径（install 阶段只把它当一个裸目标文件编）。
 *
 * 为什么必须在这里调用：环境（envp）在**进程初始栈**上，只能由入口的 argc/argv 定位；
 * libc 的 getenv/environ 要在任意调用点可用，故必须在入口捕获一次。这是**单点**。 */
extern void __boruix_init_environ(long argc, const char *const *argv);

/* C++ 静态构造/析构数组：由 `csrc/linker.ld` 定义（放在 `.data` 内，页对齐理由见该文件）。
 *
 * 为什么遍历在这里、而不是在 `crt0.S`：sysroot 的 C 链接配方走的是 **libsys 的 `_start` ->
 * `user_main`**，而 `crt0.S` 自带**强** `_start`，两者互斥（见上文 duplicate symbol 说明）。
 * 故本文件是唯一入口点——**单点**。 */
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

int user_main(long argc, const char *const *argv) {
    /* 环境必须先就位：构造函数可能读 getenv。 */
    __boruix_init_environ(argc, argv);
    /* 构造：**正序**（优先级已在链接期由 SORT_BY_INIT_PRIORITY 排好）。 */
    for (void (**p)(void) = __init_array_start; p < __init_array_end; p++) {
        (*p)();
    }
    int rc = main((int)argc, (char **)argv);
    /* 析构：**逆序**（与构造相反，C++ 契约）。 */
    for (void (**p)(void) = __fini_array_end; p > __fini_array_start;) {
        (*--p)();
    }
    return rc;
}