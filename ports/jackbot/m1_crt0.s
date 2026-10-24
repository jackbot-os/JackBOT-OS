    .section .text
    .globl _start
    .type _start,@function
_start:
    movq $stack_top, %rsp
    xorq %rbp, %rbp
    call cpython_jackbot_task
.Lhalt:
    hlt
    jmp .Lhalt

    .section .bss
    .align 16
    .space 1048576
stack_top:
