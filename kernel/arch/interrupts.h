#ifndef JANI_KERNEL_ARCH_INTERRUPTS_H
#define JANI_KERNEL_ARCH_INTERRUPTS_H

#include <stdint.h>

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

void interrupt_handler(struct interrupt_frame *frame);

void isr_divide_error(void);
void isr_invalid_opcode(void);
void isr_page_fault(void);
void isr_irq0(void);
void isr_irq1(void);

#endif
