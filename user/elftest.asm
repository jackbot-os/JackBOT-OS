; user/elftest.asm - acceptance image for the kernel ELF loader.
; Linked as a real ELF64 executable at 0x8000000000 (see Makefile), so the
; only thing that can make it run is kernel/elf.c parsing the program headers
; and mapping the PT_LOAD segments. It prints ELF_OK on the serial console
; through fd 1 and then exits the whole process group.

bits 64
default rel
global _start

section .text
_start:
    lea     rsi, [msg]
.loop:
    mov     al, [rsi]
    test    al, al
    jz      .done
    movzx   edi, al
    mov     eax, 0          ; legacy syscall 0: serial putc
    int     0x80
    inc     rsi
    jmp     .loop
.done:
    mov     eax, 35         ; SYS_EXIT_GROUP
    xor     edi, edi
    int     0x80
    jmp     .done

section .rodata
msg: db "ELF_OK", 10, 0
