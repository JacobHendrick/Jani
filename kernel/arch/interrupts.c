#include <stdint.h>

#include "../drivers/keyboard.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../lib/printk.h"
#include "../mm/layout.h"
#include "interrupts.h"

#define PAGE_FAULT_PROTECTION (1ULL << 0)
#define PAGE_FAULT_WRITE (1ULL << 1)
#define PAGE_FAULT_USER (1ULL << 2)
#define PAGE_FAULT_INSTRUCTION_FETCH (1ULL << 4)

#define TIMER_PRINT_INTERVAL 100

static const char *exception_name(uint64_t vector) {
    switch (vector) {
        case 0:
            return "divide error";
        case 6:
            return "invalid opcode";
        case 14:
            return "page fault";
        default:
            return "unknown exception";
    }
}

static uint64_t read_cr2(void) {
    uint64_t value;

    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));

    return value;
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void handle_irq0(void) {
    uint64_t ticks;

    pit_on_irq();
    ticks = pit_get_ticks();

    if ((ticks == 1) || ((ticks % TIMER_PRINT_INTERVAL) == 0)) {
        printk("tick: %d\n", (int)ticks);
    }

    pic_send_eoi(0);
}

static void handle_irq1(void) {
    uint8_t scancode;

    scancode = keyboard_read_scancode();
    printk("keyboard scancode: %x\n", (unsigned int)scancode);
    pic_send_eoi(1);
}

void interrupt_handler(struct interrupt_frame *frame) {
    if (frame->vector == PIC1_VECTOR_OFFSET) {
        handle_irq0();
        return;
    }

    if (frame->vector == (PIC1_VECTOR_OFFSET + 1)) {
        handle_irq1();
        return;
    }

    printk("\nEXCEPTION: %s\n", exception_name(frame->vector));
    printk("vector: %d\n", (int)frame->vector);
    printk("error: %x\n", (unsigned int)frame->error_code);
    printk("rip: %p\n", (void *)frame->rip);

    if (frame->vector == 14) {
        uint64_t fault_address;
        uint64_t error_code;
        const char *access_kind;

        fault_address = read_cr2();
        error_code = frame->error_code;

        if ((error_code & PAGE_FAULT_INSTRUCTION_FETCH) != 0) {
            access_kind = "instruction fetch from";
        } else if ((error_code & PAGE_FAULT_WRITE) != 0) {
            access_kind = "write to";
        } else {
            access_kind = "read from";
        }

        printk("cr2: %p\n", (void *)fault_address);
        printk("region: %s\n", memory_layout_region_name(fault_address));
        printk("cause: %s a %s page, %s mode\n",
               access_kind,
               ((error_code & PAGE_FAULT_PROTECTION) != 0)
                   ? "protected" : "not-present",
               ((error_code & PAGE_FAULT_USER) != 0) ? "user" : "kernel");
    }

    halt_forever();
}
