bits 64
org 0x8000000000

SYS_WRITE_CHAR equ 0
SYS_EXIT       equ 2

    mov eax, SYS_WRITE_CHAR
    mov edi, 'X'
    int 0x80
    ud2
    mov eax, SYS_EXIT
    int 0x80
