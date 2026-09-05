/* BORUIX freestanding C program linker script (csrc/linker.ld).
 * Kernel loader (crates/loader) requires: ET_EXEC, EM_X86_64, and every PT_LOAD
 * p_vaddr page-aligned. So each output section that can become a LOAD is aligned to
 * 0x1000 and placed at >= 0x400000 (user code base, matching Rust user ELFs).
 * Layout: .text (RX) @0x400000, .rodata (R) @ page, .data (RW) @ page, .bss (RW).
 */
ENTRY(_start)
SECTIONS {
  . = 0x400000;

  .text : ALIGN(0x1000) {
    *(.text .text.*)
    *(.rodata .rodata.*)   /* small string literals often land in .rodata; keep with text if small */
  } :text

  .rodata : ALIGN(0x1000) {
    *(.rodata .rodata.*)
  } :rodata

  .data : ALIGN(0x1000) {
    *(.data .data.*)
  } :data

  .bss : ALIGN(0x1000) {
    *(.bss .bss.*)
    *(COMMON)
  } :data

  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.debug*)
  }
}
