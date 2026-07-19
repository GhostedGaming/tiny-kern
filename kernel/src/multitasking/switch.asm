[BITS 64]

global switch_task
extern current_tcb

struc tcb
    .ksp:         resq 1
    .tsp:         resq 1
    .addr_space:  resq 1
    .next:        resq 1
    .state:       resb 1
endstruc

switch_task:
    pushfq
    cli
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rax, [rel current_tcb]
    test rax, rax
    jz .first_switch

    mov [rax + tcb.ksp], rsp

.first_switch:
    mov [rel current_tcb], rdi

    mov rsp, [rdi + tcb.ksp]

    mov rax, [rdi + tcb.addr_space]
    mov rcx, cr3

    cmp rax, rcx
    je .same_cr3

    mov cr3, rax

.same_cr3:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
    ret