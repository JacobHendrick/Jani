#ifndef JANI_KERNEL_ARCH_STACK_H
#define JANI_KERNEL_ARCH_STACK_H

#include <stdint.h>

/* Captured by start.S from %rsp before any C frame exists. */
extern uint64_t kernel_stack_top;

/* Publishes the stack size the bootloader actually granted. Call once, and
 * only after confirming the grant: with no call, kernel_stack_limit stays
 * null, which callers must read as "unknown", not as "unlimited". */
void kernel_stack_set_size(uint64_t size);

/* Lowest address code may touch, or null if the size was never published.
 * Leaves a page of headroom below the reported limit so that whatever
 * detects the overflow still has room to report it. */
uint8_t *kernel_stack_limit(void);

#endif
