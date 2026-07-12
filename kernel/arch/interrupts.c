#include <stdint.h>

#include "../drivers/keyboard.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../lib/printk.h"
#include "interrupts.h"

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
        printk("cr2: %p\n", (void *)read_cr2());
    }

    halt_forever();
}
