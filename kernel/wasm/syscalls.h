#ifndef JANI_KERNEL_WASM_SYSCALLS_H
#define JANI_KERNEL_WASM_SYSCALLS_H

#include <stdint.h>

struct component;

void jani_syscall_set_current(struct component *component);

struct component *jani_syscall_current(void);

void *jani_syscall_symbols(uint32_t *count_out);

#endif
