#ifndef JANI_KERNEL_WASM_SYSCALLS_H
#define JANI_KERNEL_WASM_SYSCALLS_H

#include <stdint.h>

struct component;
struct component_set;

void jani_syscall_set_current(struct component *component);

void jani_syscall_set_component_set(struct component_set *set);

struct component *jani_syscall_current(void);

void *jani_syscall_symbols(uint32_t *count_out);

#endif
