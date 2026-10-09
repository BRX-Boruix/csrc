/* boruix_crt.h —— C++ 静态构造/析构遍历的**单点定义**（S15）。
 *
 * 为什么放在头里、而不是一个独立的 .o：本系统有**两个**入口桥接（`user_main.c` 与
 * `user_main_argv.c`），它们各自是 **sysroot** 与 **tcc** 两条链接配方里的独立目标文件。
 * 若把遍历放进第三个 .o，两条配方都要多引用一个对象、`install` 与 `stage_assets.py` 都要
 * 多铺一份——**同步点变多**。用 `static inline` 放在共享头里，两条配方各自编译进去，
 * **定义仍只有一处**。
 *
 * 符号由链接脚本（`csrc/linker.ld`）提供；tcc 走它**自己的内部链接器**，同名约定
 * **待核实**（见 docs/TODO/3p.md）。
 *
 * ## 为什么有**两组**数组：.init_array 与 .ctors
 *
 * 现代 ELF 目标（含 target=boruix）把构造函数发进 `.init_array`；而**裸机 ELF 目标**
 * （如 target=x86_64-elf，native cc1 就是）发进**旧式的 `.ctors`**。两者语义不同：
 *
 * | 数组 | 遍历方向 | 出处 |
 * |---|---|---|
 * | `.init_array` | **正序** | 现代 ABI；优先级已由链接期 `SORT_BY_INIT_PRIORITY` 排好 |
 * | `.ctors` | **逆序** | GCC `libgcc/crtstuff.c:696`：`for (p = __CTOR_END__-1; *p != -1; p--)` |
 * | `.fini_array` | **逆序** | 与构造相反（C++ 契约） |
 * | `.dtors` | **正序** | GCC `libgcc/crtstuff.c`：自 `__DTOR_LIST__+1` 起 `while (*p) { (*p)(); p++; }` |
 *
 * 我们**不用** crtbegin/crtend，故没有 -1/0 哨兵，数组就是裸条目，遍历必须覆盖全部。
 *
 * **实测来路（2026-10）**：此前只实现了 `.init_array`/`.fini_array`，而链接脚本也只收这两个
 * ⇒ 用 target=x86_64-elf 编出的 native cc1 的 **54 个 C++ 全局构造函数一个都没跑**，
 * `object_allocator` 池停在零初始化态、按 8 字节（而非 64）步长发元素，节点互相重叠，
 * 最终在 `et_set_father` 读到被踩成 NULL 的 `rightmost_occ` 而段错误。
 * 两组数组都在这里处理，**任一编译器产物都能跑起来**。
 */
#ifndef BORUIX_CRT_H
#define BORUIX_CRT_H

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);
extern void (*__ctors_start[])(void);
extern void (*__ctors_end[])(void);
extern void (*__dtors_start[])(void);
extern void (*__dtors_end[])(void);

/* 构造。必须在 `__boruix_init_environ` **之后**调用——构造函数可能读 `getenv`。
 *
 * 两组的相对次序（先 .init_array 后 .ctors）在 ABI 里**未规定**；这里显式固定为
 * 现代组在前，并在注释里记明，避免"看起来随机"。 */
static inline void __boruix_run_ctors(void) {
    void (**p)(void);
    /* 现代组：正序（优先级已在链接期排好）。 */
    for (p = __init_array_start; p < __init_array_end; p++) {
        (*p)();
    }
    /* 旧式组：**逆序**（GCC crtstuff.c 的 __CTOR_END__-1 起、到 -1 止）。 */
    for (p = __ctors_end; p > __ctors_start;) {
        (*--p)();
    }
}

/* 析构。C++ 契约要求与构造相反。
 *
 * 旧式 .dtors 由 GCC 自 __DTOR_LIST__+1 起**正序**调用 —— 因为构造是逆序跑的，
 * 正序析构恰好等于"构造的逆序"。 */
static inline void __boruix_run_dtors(void) {
    void (**p)(void);
    /* 旧式组：正序。 */
    for (p = __dtors_start; p < __dtors_end; p++) {
        (*p)();
    }
    /* 现代组：逆序（与构造相反）。 */
    for (p = __fini_array_end; p > __fini_array_start;) {
        (*--p)();
    }
}

#endif /* BORUIX_CRT_H */
