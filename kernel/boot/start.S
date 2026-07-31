.section .text

.global _start
.extern kmain

_start:
    movq %cr0, %rax
    andq $-13, %rax
    orq $2, %rax
    movq %rax, %cr0

    movq %cr4, %rax
    orq $0x600, %rax
    movq %rax, %cr4

    andq $-16, %rsp
    call kmain

halt_forever_asm:
    cli
    hlt
    jmp halt_forever_asm