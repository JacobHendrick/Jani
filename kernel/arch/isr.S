.text

.global isr_divide_error
.global isr_invalid_opcode
.global isr_page_fault
.global isr_irq0
.global isr_irq1
.extern interrupt_handler

isr_divide_error:
    cli
    pushq $0
    pushq $0
    jmp isr_common

isr_invalid_opcode:
    cli
    pushq $0
    pushq $6
    jmp isr_common

isr_page_fault:
    cli
    pushq $14
    jmp isr_common

isr_irq0:
    pushq $0
    pushq $32
    jmp isr_common

isr_irq1:
    pushq $0
    pushq $33
    jmp isr_common

isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    cld
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call interrupt_handler
    movq %rbx, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
