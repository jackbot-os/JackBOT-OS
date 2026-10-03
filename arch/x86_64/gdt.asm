section .rodata
align 8
gdt:
    dq 0x0000000000000000
.kcode: equ $ - gdt
    dq 0x00af9a000000ffff
.kdata: equ $ - gdt
    dq 0x00cf92000000ffff
.ucode: equ $ - gdt
    dq 0x00affa000000ffff
.udata: equ $ - gdt
    dq 0x00cff2000000ffff
.pointer:
    dw $ - gdt - 1
    dq gdt

section .text
global gdt_init
gdt_init:
    lgdt [rel gdt.pointer]
    lea rax, [rel .reload]
    push 0x08
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
