/* boruix_crt.h —— C++ 静态构造/析构遍历的**单点定义**（S15）。
 *
 * 为什么放在头里、而不是一个独立的 .o：本系统有**两个**入口桥接（`user_main.c` 与
 * `user_main_argv.c`），它们各自是 **sysroot** 与 **tcc** 两条链接配方里的独立目标文件。
 * 若把遍历放进第三个 .o，两条配方都要多引用一个对象、`install` 与 `stage_assets.py` 都要
 * 多铺一份——**同步点变多**。用 `static inline` 放在共享头里，两条配方各自编译进去，
 * **定义仍只有一处**。
 *
 * 符号 `__init_array_start/end`、`__fini_array_start/end` 由链接脚本（`csrc/linker.ld`）提供；
 * tcc 走它**自己的内部链接器**，同名约定**待核实**（见 docs/TODO/3p.md）。
 */
#ifndef BORUIX_CRT_H
#define BORUIX_CRT_H

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

/* 构造：**正序**（优先级已在链接期由 `SORT_BY_INIT_PRIORITY` 排好）。
 * 必须在 `__boruix_init_environ` **之后**调用——构造函数可能读 `getenv`。 */
static inline void __boruix_run_ctors(void) {
    void (**p)(void);
    for (p = __init_array_start; p < __init_array_end; p++) {
        (*p)();
    }
}

/* 析构：**逆序**（与构造相反，C++ 契约）。 */
static inline void __boruix_run_dtors(void) {
    void (**p)(void);
    for (p = __fini_array_end; p > __fini_array_start;) {
        (*--p)();
    }
}

#endif /* BORUIX_CRT_H */
