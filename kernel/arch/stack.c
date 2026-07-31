#include "stack.h"

#define KERNEL_STACK_HEADROOM 4096

uint64_t kernel_stack_top = 0;

static uint64_t kernel_stack_size = 0;

void kernel_stack_set_size(uint64_t size) {
    if (size > KERNEL_STACK_HEADROOM) {
        kernel_stack_size = size;
    }
}

uint8_t *kernel_stack_limit(void) {
    uint64_t base;

    if ((kernel_stack_top == 0) || (kernel_stack_size == 0)) {
        return 0;
    }

    if (kernel_stack_size >= kernel_stack_top) {
        return 0;
    }

    base = kernel_stack_top - kernel_stack_size;

    return (uint8_t *)(uintptr_t)(base + KERNEL_STACK_HEADROOM);
}
