; user/uthread.asm — ring-3 acceptance test for the JackBOT pthread ABI.
;
; It exercises exactly the calls the musl port makes:
;   arch_prctl(ARCH_SET_FS)  -> TLS base really lands in the fs MSR
;   mmap                     -> an anonymous thread stack appears
;   clone(CLONE_VM|SETTLS|PARENT_SETTID|CHILD_CLEARTID)
;   futex(WWAIT/WAKE)        -> parent sleeps, child wakes it
;   exit(thread)             -> *ctid = 0 and the futex is woken
; Everything is reported on the serial console through fd 1.

bits 64
org 0x8000000000

%define SYS_YIELD       1
%define SYS_CLONE       33
%define SYS_EXIT        34
%define SYS_FUTEX       36
%define SYS_MMAP        38
%define SYS_ARCH_PRCTL  43
%define SYS_WRITE       48

%define CLONE_VM             0x00000100
%define CLONE_SETTLS         0x00080000
%define CLONE_PARENT_SETTID  0x00100000
%define CLONE_CHILD_CLEARTID 0x00200000

%define FUTEX_WAIT 0
%define FUTEX_WAKE 1

; stage marker on the serial console (legacy syscall 0 = putc)
%macro MARK 1
    push rax
    push rdi
    mov eax, 0
    mov edi, %1
    int 0x80
    pop rdi
    pop rax
%endmacro

start:
    MARK 'a'
    ; ---- 1) TLS: set fs base to our own block and read it back ----
    mov eax, SYS_ARCH_PRCTL
    mov edi, 0x1002                 ; ARCH_SET_FS
    lea rsi, [rel tls_main]
    int 0x80
    test rax, rax
    jnz fail_tls
    mov rax, [fs:0]
    cmp rax, 0x4d41494e             ; 'MAIN'
    jne fail_tls
    MARK 'b'

    ; ---- 2) mmap a 16 KiB stack for the new thread ----
    mov eax, SYS_MMAP
    xor edi, edi                    ; let the kernel choose the address
    mov esi, 16384
    xor edx, edx
    xor r10d, r10d
    int 0x80
    test rax, rax
    js fail_mmap
    MARK 'c'
    mov r15, rax
    add r15, 16384
    and r15, -16
    sub r15, 8
    mov qword [r15], 0              ; the arg slot __clone would push
    MARK 'd'

    ; ---- 3) clone the thread ----
    mov edi, CLONE_VM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID
    mov rsi, r15
    lea rdx, [rel ptid]
    lea r10, [rel tls_child]
    lea r8, [rel ctid]
    lea r9, [rel child_body]
    mov eax, SYS_CLONE
    int 0x80
    MARK 'e'
    test eax, eax
    jz child_body                   ; child: rax = 0, rsp = thread stack

    ; ---- 4) parent ----
    test rax, rax
    js fail_clone
    mov r14, rax                    ; child tid
    mov eax, [rel ptid]
    cmp eax, r14d
    jne fail_ptid
    MARK 'f'

    ; sleep on word until the child stores 1 and wakes us
    mov eax, SYS_FUTEX
    lea rdi, [rel futex_word]
    mov esi, FUTEX_WAIT
    xor edx, edx                    ; expected value 0
    xor r10d, r10d
    int 0x80
    cmp dword [rel futex_word], 1
    jne fail_wait
    MARK 'g'

    ; join without a syscall: wait for the child to clear *ctid on exit.
    ; CLONE_CHILD_CLEARTID is the only join signal musl relies on.
    mov ecx, 200000
join_loop:
    mov eax, [rel ctid]
    test eax, eax
    jz joined
    dec ecx
    jz fail_join
    mov eax, SYS_FUTEX
    lea rdi, [rel ctid]
    mov esi, FUTEX_WAIT
    mov edx, [rel ctid]
    xor r10d, r10d
    int 0x80
    mov eax, SYS_YIELD
    int 0x80
    jmp join_loop
joined:
    MARK 'h'
    mov eax, SYS_WRITE
    mov edi, 1
    lea rsi, [rel msg_ok]
    mov edx, msg_ok_len
    int 0x80
    mov eax, SYS_EXIT
    xor edi, edi
    int 0x80
    hlt

child_body:
    MARK 'i'
    ; ---- 5) child: prove CLONE_SETTLS moved the fs base ----
    add rsp, 8                      ; drop the arg slot
    mov rax, [fs:0]
    cmp rax, 0x43484c44             ; 'CHLD'
    jne fail_ctls
    mov dword [rel futex_word], 1
    mov eax, SYS_FUTEX
    lea rdi, [rel futex_word]
    mov esi, FUTEX_WAKE
    mov edx, 1
    int 0x80
    mov eax, SYS_WRITE
    mov edi, 1
    lea rsi, [rel msg_c]
    mov edx, msg_c_len
    int 0x80
    ; SYS_exit must clear *ctid and wake the joiner before it vanishes
    mov eax, SYS_EXIT
    xor edi, edi
    int 0x80
    hlt

fail_ctls:
    lea rsi, [rel msg_fctls]
    mov edx, msg_fctls_len
    jmp child_fail

fail_tls:
    lea rsi, [rel msg_ftls]
    mov edx, msg_ftls_len
    jmp fail
fail_mmap:
    lea rsi, [rel msg_fmmap]
    mov edx, msg_fmmap_len
    jmp fail
fail_clone:
    lea rsi, [rel msg_fclone]
    mov edx, msg_fclone_len
    jmp fail
fail_ptid:
    lea rsi, [rel msg_fptid]
    mov edx, msg_fptid_len
    jmp fail
fail_wait:
    lea rsi, [rel msg_fwait]
    mov edx, msg_fwait_len
    jmp fail
fail_join:
    lea rsi, [rel msg_fjoin]
    mov edx, msg_fjoin_len
    jmp fail

child_fail:
    mov eax, SYS_WRITE
    mov edi, 1
    int 0x80
    mov eax, SYS_EXIT
    mov edi, 1
    int 0x80
    hlt

fail:
    mov eax, SYS_WRITE
    mov edi, 1
    int 0x80
    mov eax, SYS_EXIT
    mov edi, 1
    int 0x80
    hlt

align 8
tls_main:  dq 0x4d41494e, 0, 0, 0
tls_child: dq 0x43484c44, 0, 0, 0
futex_word:     dd 0
ptid:      dd 0
ctid:      dd 0

msg_ok:   db "PTHREAD_OK", 10
msg_ok_len equ $ - msg_ok
msg_c:    db "+C"
msg_c_len equ $ - msg_c
msg_ftls: db "PTHREAD_FAIL tls", 10
msg_ftls_len equ $ - msg_ftls
msg_fmmap: db "PTHREAD_FAIL mmap", 10
msg_fmmap_len equ $ - msg_fmmap
msg_fclone: db "PTHREAD_FAIL clone", 10
msg_fclone_len equ $ - msg_fclone
msg_fptid: db "PTHREAD_FAIL ptid", 10
msg_fptid_len equ $ - msg_fptid
msg_fwait: db "PTHREAD_FAIL futex_wait", 10
msg_fwait_len equ $ - msg_fwait
msg_fjoin: db "PTHREAD_FAIL join", 10
msg_fjoin_len equ $ - msg_fjoin
msg_fctls: db "PTHREAD_FAIL child_tls", 10
msg_fctls_len equ $ - msg_fctls
