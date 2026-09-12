#ifndef JANI_KERNEL_WASM_INSTANCE_STATE_H
#define JANI_KERNEL_WASM_INSTANCE_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "component.h"

#define INSTANCE_STATE_MAGIC UINT64_C(0x4A414E495F535441)
#define INSTANCE_STATE_FORMAT_VERSION UINT32_C(1)
#define INSTANCE_STATE_HEADER_SIZE 64u

struct instance_state_header {
    uint64_t magic;
    uint32_t format_version;
    uint32_t header_size;

    uint64_t logical_time;
    uint64_t timer_deadline;

    uint32_t timer_armed;
    uint32_t mailbox_used;

    uint64_t memory_size;

    uint32_t payload_crc32c;
    uint32_t _reserved;

    uint64_t _padding;
};

_Static_assert(
    sizeof(struct instance_state_header) == INSTANCE_STATE_HEADER_SIZE,
    "instance state header must be exactly 64 bytes"
);

size_t instance_state_size(size_t memory_size, uint32_t mailbox_used);
int instance_state_bytes_validate(const uint8_t *bytes, size_t length);

int instance_state_header_validate(
    const uint8_t *bytes,
    size_t byte_count,
    size_t *memory_offset_out,
    size_t *memory_size_out
);

int instance_state_serialize(
    const struct component *component,
    const uint8_t *memory,
    size_t memory_size,
    uint8_t *out,
    size_t capacity,
    size_t *written_out
);

int instance_state_deserialize(
    struct component *component,
    uint8_t *memory,
    size_t memory_size,
    const uint8_t *bytes,
    size_t byte_count
);

#endif
