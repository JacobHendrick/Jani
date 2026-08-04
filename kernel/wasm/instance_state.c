#include "instance_state.h"

#include "../lib/string.h"
#include "../obj/object_header.h"

size_t instance_state_size(size_t memory_size, uint32_t mailbox_used) {
    return INSTANCE_STATE_HEADER_SIZE + (size_t)mailbox_used + memory_size;
}

int instance_state_header_validate(
    const uint8_t *bytes,
    size_t byte_count,
    size_t *memory_offset_out,
    size_t *memory_size_out
) {
    struct instance_state_header header;
    uint64_t payload_bytes;
    uint64_t expected_total;
    uint32_t calculated_crc;

    if ((bytes == NULL) || (byte_count < INSTANCE_STATE_HEADER_SIZE)) {
        return 0;
    }

    memcpy(&header, bytes, INSTANCE_STATE_HEADER_SIZE);

    if ((header.magic != INSTANCE_STATE_MAGIC) ||
        (header.format_version != INSTANCE_STATE_FORMAT_VERSION) ||
        (header.header_size != INSTANCE_STATE_HEADER_SIZE) ||
        (header._reserved != 0) ||
        (header._padding != 0)) {
        return 0;
    }

    if (header.mailbox_used > COMPONENT_MAILBOX_BYTES) {
        return 0;
    }

    if ((header.timer_armed != 0) && (header.timer_armed != 1)) {
        return 0;
    }

    payload_bytes = (uint64_t)header.mailbox_used + header.memory_size;
    expected_total = (uint64_t)INSTANCE_STATE_HEADER_SIZE + payload_bytes;

    if (expected_total != (uint64_t)byte_count) {
        return 0;
    }

    calculated_crc = object_crc32c(bytes + INSTANCE_STATE_HEADER_SIZE,
                                   (size_t)payload_bytes);
    if (calculated_crc != header.payload_crc32c) {
        return 0;
    }

    if (memory_offset_out != NULL) {
        *memory_offset_out = INSTANCE_STATE_HEADER_SIZE +
                             (size_t)header.mailbox_used;
    }
    if (memory_size_out != NULL) {
        *memory_size_out = (size_t)header.memory_size;
    }

    return 1;
}

int instance_state_serialize(
    const struct component *component,
    const uint8_t *memory,
    size_t memory_size,
    uint8_t *out,
    size_t capacity,
    size_t *written_out
) {
    (void)component;
    (void)memory;
    (void)memory_size;
    (void)out;
    (void)capacity;
    (void)written_out;
    return 0;
}

int instance_state_deserialize(
    struct component *component,
    uint8_t *memory,
    size_t memory_size,
    const uint8_t *bytes,
    size_t byte_count
) {
    (void)component;
    (void)memory;
    (void)memory_size;
    (void)bytes;
    (void)byte_count;
    return 0;
}
