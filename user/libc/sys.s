    .text
    .globl __jb_sys
__jb_sys:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    int $0x80
    ret
