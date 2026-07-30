#ifndef JANI_KERNEL_OBJ_OBJECT_TABLE_H
#define JANI_KERNEL_OBJ_OBJECT_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "object_id.h"

struct object_table_entry {
    struct object_id id;
    uint64_t version;
    uint64_t first_sector;
    uint64_t sector_count;
};

struct object_table {
    struct object_table_entry *entries;
    size_t count;
    size_t capacity;
};

void object_table_init(
    struct object_table *table,
    struct object_table_entry *storage,
    size_t capacity
);

const struct object_table_entry *object_table_find(
    const struct object_table *table,
    struct object_id id
);

int object_table_upsert(
    struct object_table *table,
    struct object_table_entry entry
);

#endif
