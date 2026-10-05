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

int user_main(long argc, const char *const *argv) {
    return main((int)argc, (char **)argv);
}