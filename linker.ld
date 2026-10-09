/* BORUIX 用户态程序链接脚本（csrc/linker.ld）。
 *
 * 供 sysroot 的 C 与 Rust 两条路径共用：install 会把它装到 <prefix>/lib/linker.ld，
 * C 驱动与 cargo 配置都引用那一份。
 *
 * **必须用独立的 `. = ALIGN(0x1000);` 语句推进位置计数器**，而不是写成
 * `section : ALIGN(0x1000) { ... }`。后者把 ALIGN 放在段内容里，实测不足以让段起始
 * 页对齐，于是两个 PT_LOAD 会共享同一页；内核加载器按页映射两次即返回
 * AlreadyExists(17)，用户侧表现为 `cannot execute ...: already exists`。
 * 既有可用脚本（shell/linker.ld 等）用的正是本文件下面的写法。
 *
 * .got 必须保留并映射（RW）：`memcpy` 等经 PLT 的间接跳转会读 GOT 条目，丢弃它会落到
 * 未映射的 vaddr 0 触发 page fault（内核 linker.ld 亦不丢弃）。
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text .text.*)
    }

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);

    /* RELRO 段必须**显式安置**：不写它就会成为孤儿段，被 lld 紧贴 .text 放置（不页对齐），
     * 从而与前一 LOAD 共享页 → 内核 AlreadyExists(17)。 */
    .data.rel.ro : {
        *(.data.rel.ro .data.rel.ro.*)
    }

    . = ALIGN(0x1000);

    .data : {
        /* C++ 静态构造/析构数组（.init_array/.fini_array）。
         *
         * **必须放在既有 `.data` 里，而不是另起一个段**：本文件头部记录的实测坑——每个新段前
         * 必须有独立的 `. = ALIGN(0x1000);`，否则会与前一个 LOAD 共享页，内核加载器按页映射
         * 两次即返回 AlreadyExists(17)。放在 .data 内既拿到 RW 属性，又不新增 LOAD。
         *
         * `SORT_BY_INIT_PRIORITY` 按构造优先级排序（C++ 契约要求）；`KEEP` 防止被 --gc-sections
         * 丢弃——这些条目**没有符号引用**，只靠链接脚本保留。
         * 符号名 `__init_array_start/end` 是 GCC/Clang 的通行约定（crtbegin/crtend 也用）。 */
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;

        /* **旧式 .ctors/.dtors 也必须收**（2026-10 实测的真缺陷）。
         *
         * 来路：native cc1（target=x86_64-elf，裸机）把 C++ 全局构造函数发射进
         * **.ctors**，而本脚本此前只收 .init_array ⇒ 54 个构造函数**一个都没跑**，
         * 所有 C++ 全局对象停在零初始化态。症状不是"立刻崩"，而是**远端的诡异崩溃**：
         * cc1 在 GIMPLE cfg pass 里 et_nodes/et_occurrences 这两个 object_allocator 池
         * （带构造函数的类）步长错乱 —— 实测 sizeof(et_node)=64 而池按 8 字节步长发元素，
         * 四个节点互相重叠，最后 et_set_father 读到被踩成 NULL 的 rightmost_occ 而段错误。
         *
         * **为什么修在链接脚本而不是改编译器**：链接脚本是"必须接受编译器所发之物"的
         * **单点**。任何第三方裸机编译器（x86_64-elf、newlib 系）都会发 .ctors；
         * 改配置只能救我们自己这一条链，且换编译器/换配置就复发。
         *
         * **顺序语义**（照 GCC 自己的 libgcc/crtstuff.c:696 与 :752）：
         *   .ctors 由 "for (p = __CTOR_END__-1; *p != -1; p--)" **反向**调用；
         *   .dtors 由 "while (*p) { (*p)(); p++; }" 自 __DTOR_LIST__+1 **正向**调用。
         * 我们不用 crtbegin/crtend，故没有 -1/0 哨兵，数组就是裸条目 ——
         * 遍历方向在 csrc/boruix_crt.h 里按同一约定实现（那边有完整注释）。 */
        __ctors_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        __ctors_end = .;
        __dtors_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
        __dtors_end = .;
        *(.data .data.*)
    }

    . = ALIGN(0x1000);

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    . = ALIGN(0x1000);

    .got : {
        *(.got .got.*)
        *(.got.plt)
    }

    . = ALIGN(0x1000);

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.debug*)
    }
}