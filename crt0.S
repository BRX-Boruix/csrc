.intel_syntax noprefix
/* BORUIX C runtime entry (csrc/crt0.S). ABI: kernel loads ELF, sets rsp to point at
 * argc on user stack ([rsp]=argc, [rsp+8]=argv), jumps to e_entry=_start.
 * Mirrors libsys start.rs: xor rbp; mov rdi,[rsp](argc); lea rsi,[rsp+8](argv);
 * and rsp,-16; call main; mov rdi,rax(exit code); call exit; ud2. */
.section .text
.global _start
.type _start, @function
_start:
    xor rbp, rbp
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    and rsp, -16
    call main
    mov rdi, rax
    call exit
    ud2
.size _start, . - _start
