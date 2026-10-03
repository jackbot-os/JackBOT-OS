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
