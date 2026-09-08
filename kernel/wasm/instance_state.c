#include "instance_state.h"

#include "../lib/string.h"
#include "../obj/object_header.h"

size_t instance_state_size(size_t memory_size, uint32_t mailbox_used) {
    if (mailbox_used > COMPONENT_MAILBOX_BYTES ||
        memory_size > SIZE_MAX - INSTANCE_STATE_HEADER_SIZE - mailbox_used) return 0;
    return INSTANCE_STATE_HEADER_SIZE + (size_t)mailbox_used + memory_size;
}

int instance_state_header_validate(
    const uint8_t *bytes,
    size_t byte_count,
    size_t *memory_offset_out,
    size_t *memory_size_out
) {
    struct instance_state_header header;
    if (!instance_state_bytes_validate(bytes, byte_count)) return 0;
    memcpy(&header, bytes, INSTANCE_STATE_HEADER_SIZE);

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
    struct instance_state_header header;
    size_t total;
    size_t memory_offset;

    if ((component == NULL) || (out == NULL) || (written_out == NULL)) {
        return 0;
    }
    if ((memory_size != 0) && (memory == NULL)) {
        return 0;
    }
    if (component->mailbox_used > COMPONENT_MAILBOX_BYTES) {
        return 0;
    }

    total = instance_state_size(memory_size, component->mailbox_used);
    if (total == 0 || total > capacity) {
        return 0;
    }

    memory_offset = INSTANCE_STATE_HEADER_SIZE +
                    (size_t)component->mailbox_used;

    if (component->mailbox_used != 0) {
        memcpy(out + INSTANCE_STATE_HEADER_SIZE, component->mailbox,
               (size_t)component->mailbox_used);
    }
    if (memory_size != 0) {
        memcpy(out + memory_offset, memory, memory_size);
    }

    memset(&header, 0, sizeof(header));
    header.magic = INSTANCE_STATE_MAGIC;
    header.format_version = INSTANCE_STATE_FORMAT_VERSION;
    header.header_size = INSTANCE_STATE_HEADER_SIZE;
    header.logical_time = component->logical_time;
    header.timer_deadline = component->timer_deadline;
    header.timer_armed = (component->timer_armed != 0) ? 1u : 0u;
    header.mailbox_used = component->mailbox_used;
    header.memory_size = (uint64_t)memory_size;
    header.payload_crc32c = object_crc32c(
        out + INSTANCE_STATE_HEADER_SIZE,
        total - INSTANCE_STATE_HEADER_SIZE
    );

    memcpy(out, &header, INSTANCE_STATE_HEADER_SIZE);

    *written_out = total;
    return 1;
}

int instance_state_deserialize(
    struct component *component,
    uint8_t *memory,
    size_t memory_size,
    const uint8_t *bytes,
    size_t byte_count
) {
    struct instance_state_header header;
    size_t memory_offset;
    size_t saved_memory_size;

    if (component == NULL) {
        return 0;
    }
    if (!instance_state_header_validate(bytes, byte_count, &memory_offset,
                                        &saved_memory_size)) {
        return 0;
    }
    if ((memory_size != 0) && (memory == NULL)) {
        return 0;
    }
    if (saved_memory_size > memory_size) {
        return 0;
    }

    memcpy(&header, bytes, INSTANCE_STATE_HEADER_SIZE);

    if (saved_memory_size != 0) {
        memcpy(memory, bytes + memory_offset, saved_memory_size);
    }
    if (memory_size > saved_memory_size) {
        memset(memory + saved_memory_size, 0,
               memory_size - saved_memory_size);
    }

    memset(component->mailbox, 0, COMPONENT_MAILBOX_BYTES);
    if (header.mailbox_used != 0) {
        memcpy(component->mailbox, bytes + INSTANCE_STATE_HEADER_SIZE,
               (size_t)header.mailbox_used);
    }

    component->mailbox_used = header.mailbox_used;
    component->logical_time = header.logical_time;
    component->timer_deadline = header.timer_deadline;
    component->timer_armed = header.timer_armed;

    return 1;
}
