bits 64
org 0x8000000000

SYS_WRITE_CHAR equ 0
SYS_YIELD      equ 1
SYS_EXIT       equ 2

    mov rcx, 5
.loop:
    mov eax, SYS_WRITE_CHAR
    mov edi, 'U'
    int 0x80
    dec rcx
    jz .done
    mov eax, SYS_YIELD
    int 0x80
    jmp .loop
.done:
    mov eax, SYS_EXIT
    int 0x80
    jmp .done
