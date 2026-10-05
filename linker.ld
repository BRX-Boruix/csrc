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