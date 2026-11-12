section .text
bits 64
extern thread_exit

global context_switch
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

global user_enter
user_enter:
    iretq

; Clone children come back to the instruction after the int 0x80 in the
; parent, under their own user stack, with rax = 0 and r9 still holding the
; entry function (musl's __clone relies on both). The stack carries a pointer
; to a two-word block {rax, r9} owned by the thread slot.
global user_enter_clone
user_enter_clone:
    pop rdi
    mov rax, [rdi]
    mov r9, [rdi+8]
    iretq

global thread_trampoline
thread_trampoline:
    sti
    mov rdi, rbx
    mov rax, rbp
    call rax
    xor edi, edi
    call thread_exit
.dead:
    hlt
    jmp .dead
