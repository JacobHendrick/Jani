#ifndef JANI_KERNEL_OBJ_OBJECT_STORE_H
#define JANI_KERNEL_OBJ_OBJECT_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "object_header.h"
#include "object_table.h"

#define OBJECT_STORE_SECTOR_SIZE 512u
#define OBJECT_STORE_SNAPSHOT_LIMIT 8u

struct object_store_io {
    void *context;
    uint64_t sector_count;
    int (*read_sector)(void *context, uint64_t sector, uint8_t *buffer);
    int (*write_sector)(void *context, uint64_t sector,
                        const uint8_t *buffer);
    int (*flush)(void *context);
};

struct object_store_snapshot {
    uint64_t id;
    uint64_t table_sector;
    uint64_t table_count;
};

struct object_store {
    struct object_store_io io;
    struct object_table table;
    struct object_table_entry *scratch_entries;
    size_t table_capacity;
    uint8_t *cache_buffer;
    size_t cache_capacity;
    size_t cache_size;
    struct object_id cache_id;
    uint64_t cache_version;
    int cache_valid;
    uint8_t *bitmap_buffer;
    size_t bitmap_capacity;
    uint64_t next_sector;
    uint64_t current_table_sector;
    uint64_t current_generation;
    uint64_t superblock_sequence;
    uint64_t active_superblock_sector;
    struct object_store_snapshot snapshots[OBJECT_STORE_SNAPSHOT_LIMIT];
};

int object_store_format(
    struct object_store *store,
    struct object_store_io io,
    struct object_table_entry *table_entries,
    struct object_table_entry *scratch_entries,
    size_t table_capacity,
    uint8_t *cache_buffer,
    size_t cache_capacity,
    uint8_t *bitmap_buffer,
    size_t bitmap_capacity
);

int object_store_mount(
    struct object_store *store,
    struct object_store_io io,
    struct object_table_entry *table_entries,
    struct object_table_entry *scratch_entries,
    size_t table_capacity,
    uint8_t *cache_buffer,
    size_t cache_capacity,
    uint8_t *bitmap_buffer,
    size_t bitmap_capacity
);

int object_store_put(
    struct object_store *store,
    struct object_id id,
    struct object_id type_id,
    struct object_id creator_id,
    struct object_id modifier_id,
    uint64_t logical_timestamp,
    const uint8_t *payload,
    size_t payload_size
);

int object_store_get(
    struct object_store *store,
    struct object_id id,
    struct object_header *header_out,
    const uint8_t **payload_out,
    size_t *payload_size_out
);

int object_store_snapshot_create(
    struct object_store *store,
    uint64_t *snapshot_id_out
);

int object_store_snapshot_rollback(
    struct object_store *store,
    uint64_t snapshot_id
);

int object_store_snapshot_discard(
    struct object_store *store,
    uint64_t snapshot_id
);

int object_store_collect(struct object_store *store);

int object_store_snapshot_prune(struct object_store *store, size_t keep);

#endif
