#include <stdint.h>
#include <stddef.h>

#include "../../kernel/wasm/syscall_args.h"

static uint64_t take_u64(const uint8_t *data, size_t size, size_t *cursor) {
    uint64_t value;
    size_t index;

    value = 0;
    for (index = 0; index < 8; index++) {
        value <<= 8;
        if (*cursor < size) {
            value |= data[*cursor];
            (*cursor)++;
        }
    }

    return value;
}

static uint32_t take_u32(const uint8_t *data, size_t size, size_t *cursor) {
    uint32_t value;
    size_t index;

    value = 0;
    for (index = 0; index < 4; index++) {
        value <<= 8;
        if (*cursor < size) {
            value |= data[*cursor];
            (*cursor)++;
        }
    }

    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t cursor;
    uint64_t limit;
    uint64_t object_size;
    uint32_t start;
    uint32_t length;
    uint32_t offset;
    uint32_t slot_count;
    uint32_t clamped;
    int32_t slot;
    int accepted;
    int expected;

    cursor = 0;
    limit = take_u64(data, size, &cursor);
    object_size = take_u64(data, size, &cursor);
    start = take_u32(data, size, &cursor);
    length = take_u32(data, size, &cursor);
    offset = take_u32(data, size, &cursor);
    slot_count = take_u32(data, size, &cursor);
    slot = (int32_t)take_u32(data, size, &cursor);

    accepted = jani_syscall_check_span(limit, start, length);
    expected = (((uint64_t)start + (uint64_t)length) <= limit) ? 1 : 0;
    if (accepted != expected) {
        __builtin_trap();
    }

    accepted = jani_syscall_check_slot(slot_count, slot);
    expected = ((slot >= 0) && ((uint32_t)slot < slot_count)) ? 1 : 0;
    if (accepted != expected) {
        __builtin_trap();
    }

    accepted = jani_syscall_check_optional_slot(slot_count, slot);
    expected = (slot == -1) ? 1 :
               (((slot >= 0) && ((uint32_t)slot < slot_count)) ? 1 : 0);
    if (accepted != expected) {
        __builtin_trap();
    }

    clamped = 0xFFFFFFFFu;
    accepted = jani_syscall_clamp_read(object_size, offset, length, &clamped);
    if ((uint64_t)offset > object_size) {
        if ((accepted != 0) || (clamped != 0)) {
            __builtin_trap();
        }
    } else {
        uint64_t remaining;
        uint64_t want;

        remaining = object_size - (uint64_t)offset;
        want = ((uint64_t)length < remaining) ? (uint64_t)length : remaining;

        if ((accepted != 1) || ((uint64_t)clamped != want)) {
            __builtin_trap();
        }

        if (!jani_syscall_check_span(object_size, offset, clamped)) {
            __builtin_trap();
        }
    }

    accepted = jani_syscall_check_transfer(limit, start, object_size, offset,
                                           length);
    expected = (jani_syscall_check_span(limit, start, length) &&
                jani_syscall_check_span(object_size, offset, length)) ? 1 : 0;
    if (accepted != expected) {
        __builtin_trap();
    }

    return 0;
}
