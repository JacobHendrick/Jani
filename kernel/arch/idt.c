#include <stdint.h>

#include "idt.h"
#include "interrupts.h"

#define IDT_ENTRY_COUNT 256

#define IDT_GATE_INTERRUPT 0x8E

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRY_COUNT];
static struct idt_descriptor idt_descriptor;

__attribute__((naked))
static void idt_stub(void) {
    __asm__ volatile (
        "cli\n"
        "1:\n"
        "hlt\n"
        "jmp 1b\n"
    );
}

static void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t attributes) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].reserved = 0;
}

static void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idt_descriptor) : "memory");
}

void idt_init(void) {
    uint16_t vector;
    uint64_t handler;

    handler = (uint64_t)(uintptr_t)idt_stub;

    for (vector = 0; vector < IDT_ENTRY_COUNT; vector++) {
        idt_set_gate((uint8_t)vector, handler, IDT_GATE_INTERRUPT);
    }

    idt_set_gate(0, (uint64_t)(uintptr_t)isr_divide_error, IDT_GATE_INTERRUPT);
    idt_set_gate(6, (uint64_t)(uintptr_t)isr_invalid_opcode, IDT_GATE_INTERRUPT);
    idt_set_gate(14, (uint64_t)(uintptr_t)isr_page_fault, IDT_GATE_INTERRUPT);
    idt_set_gate(19, (uint64_t)(uintptr_t)isr_simd_error, IDT_GATE_INTERRUPT);
    idt_set_gate(32, (uint64_t)(uintptr_t)isr_irq0, IDT_GATE_INTERRUPT);
    idt_set_gate(33, (uint64_t)(uintptr_t)isr_irq1, IDT_GATE_INTERRUPT);

    idt_descriptor.size = sizeof(idt) - 1;
    idt_descriptor.offset = (uint64_t)(uintptr_t)idt;

    idt_load();
}
