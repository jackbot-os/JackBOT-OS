section .multiboot_header
align 8
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    align 8
    dw 5
    dw 1
    dd 20
    dd 1024
    dd 768
    dd 32

    align 8
    dw 0
    dw 0
    dd 8

header_end:

section .bss
align 4096
global p4_table
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096
p2_mmio:
    resb 4096
stack_bottom:
    resb 262144
stack_top:

section .rodata
align 8
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
bits 32
extern kmain
global _start
_start:
    cli
    mov esp, stack_top
    push 0
    push eax
    push 0
    push ebx
    call check_cpuid
    call check_long_mode
    call set_up_page_tables
    call enable_paging
    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

set_up_page_tables:
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax
    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax
    mov eax, p2_mmio
    or eax, 0b11
    mov [p3_table + 3 * 8], eax
    mov ecx, 0
.map_p2:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011
    mov [p2_table + ecx * 8], eax
    inc ecx

    cmp ecx, 512
    jl .map_p2
    mov dword [p2_mmio + 502 * 8], 0xfec0009b
    mov dword [p2_mmio + 502 * 8 + 4], 0
    mov dword [p2_mmio + 503 * 8], 0xfee0009b
    mov dword [p2_mmio + 503 * 8 + 4], 0
    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz no_cpuid
    ret

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz no_long_mode
    ret

no_cpuid:
    mov al, "1"
    jmp boot_error
no_long_mode:
    mov al, "2"
boot_error:
    mov word [0xb8000], 0x0F45
    mov byte  [0xb8002], al
    mov byte  [0xb8003], 0x0F
.hang:
    hlt
    jmp .hang

section .text64
bits 64
long_mode_start:
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, cr0
    and ax, 0xFFFB
    or  ax, 0x2
    mov cr0, rax
    mov rax, cr4
    or  ax, 0x600
    mov cr4, rax

    pop rsi
    pop rdi
    call kmain
.dead:
    hlt
    jmp .dead
