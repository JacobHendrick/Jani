#ifndef JANI_KERNEL_OBJ_WAL_H
#define JANI_KERNEL_OBJ_WAL_H

#include <stddef.h>
#include <stdint.h>

#define OBJECT_WAL_MAGIC UINT64_C(0x4A414E4957414C31)
#define OBJECT_WAL_FORMAT_VERSION UINT32_C(1)
#define OBJECT_WAL_RECORD_SIZE 512u

struct object_wal_record {
    uint64_t magic;
    uint32_t format_version;
    uint32_t flags;
    uint64_t table_sector;
    uint64_t table_count;
    uint64_t generation;
    uint32_t checksum;
    uint32_t reserved;
    uint8_t padding[464];
};

_Static_assert(
    sizeof(struct object_wal_record) == OBJECT_WAL_RECORD_SIZE,
    "WAL records must occupy exactly one sector"
);

int object_wal_validate(const uint8_t *bytes, size_t byte_count);

#endif
