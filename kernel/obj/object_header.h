#ifndef JANI_KERNEL_OBJ_OBJECT_HEADER_H
#define JANI_KERNEL_OBJ_OBJECT_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "object_id.h"

#define OBJECT_HEADER_MAGIC UINT64_C(0x4A414E495F4F424A)
#define OBJECT_HEADER_FORMAT_VERSION UINT32_C(1)
#define OBJECT_HEADER_SIZE 128u

struct object_header {
    uint64_t magic;
    uint32_t format_version;
    uint32_t reference_count;

    struct object_id id;
    struct object_id type_id;

    uint64_t version;
    uint64_t payload_size;
    uint32_t payload_crc32c;
    uint32_t flags;

    struct object_id creator_id;
    struct object_id modifier_id;
    uint64_t logical_timestamp;
    uint64_t _padding;
    uint64_t _reserved;
};

_Static_assert(
    sizeof(struct object_header) == OBJECT_HEADER_SIZE,
    "object header must be exactly 128 bytes"
);

int object_header_validate(
    const uint8_t *bytes,
    size_t byte_count
);

uint32_t object_crc32c(
    const uint8_t *bytes,
    size_t byte_count
);

#endif
