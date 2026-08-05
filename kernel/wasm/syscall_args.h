#ifndef JANI_KERNEL_WASM_SYSCALL_ARGS_H
#define JANI_KERNEL_WASM_SYSCALL_ARGS_H

#include <stdint.h>

int jani_syscall_check_span(uint64_t limit, uint32_t start, uint32_t length);

int jani_syscall_check_slot(uint32_t slot_count, int32_t slot);

int jani_syscall_check_optional_slot(uint32_t slot_count, int32_t slot);

int jani_syscall_clamp_read(
    uint64_t object_size,
    uint32_t offset,
    uint32_t length,
    uint32_t *length_out
);

int jani_syscall_check_transfer(
    uint64_t memory_size,
    uint32_t pointer,
    uint64_t object_size,
    uint32_t offset,
    uint32_t length
);

int jani_syscall_deadline(
    uint64_t logical_time,
    uint64_t delay_ticks,
    uint64_t *deadline_out
);

#endif
