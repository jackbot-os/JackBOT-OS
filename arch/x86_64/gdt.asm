section .data
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
.tss: equ $ - gdt
tss_desc:
    dq 0
    dq 0
gdt_pointer:
    dw $ - gdt - 1
    dq gdt

section .bss
align 16
global tss64
tss64:
    resb 104

section .text
global gdt_init
global tss_set_rsp0
gdt_init:
    lea r10, [rel tss64]
    lea rsi, [rel tss_desc]
    mov word [rsi], 103
    mov [rsi + 2], r10w
    mov rax, r10
    shr rax, 16
    mov [rsi + 4], al
    mov byte [rsi + 5], 0x89
    mov byte [rsi + 6], 0
    mov rax, r10
    shr rax, 24
    mov [rsi + 7], al
    mov rax, r10
    shr rax, 32
    mov [rsi + 8], eax
    mov dword [rsi + 12], 0

    lgdt [rel gdt_pointer]
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
    mov ax, 0x28
    ltr ax
    ret

tss_set_rsp0:
    mov [rel tss64 + 4], rdi
    ret
