#ifndef JANI_KERNEL_OBJ_OBJECT_STORE_H
#define JANI_KERNEL_OBJ_OBJECT_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "object_header.h"
#include "object_table.h"

#define OBJECT_STORE_SECTOR_SIZE 512u
#define OBJECT_STORE_SNAPSHOT_LIMIT 8u
#define OBJECT_STORE_CACHE_SLOTS 16u
#define OBJECT_STORE_PUT_BATCH_MAX 8u


struct object_cache_slot {
    struct object_id id;
    uint64_t version;
    size_t offset;
    size_t length;
    uint64_t last_access;
};

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

/* Requests and payload bytes are borrowed only for the duration of a call. */
struct object_store_put_request {
    struct object_id id;
    struct object_id type_id;
    struct object_id creator_id;
    struct object_id modifier_id;
    uint64_t logical_timestamp;
    const uint8_t *payload;
    size_t payload_size;
};

enum object_store_batch_result {
    OBJECT_STORE_BATCH_REJECTED = 0,
    OBJECT_STORE_BATCH_COMMITTED = 1,
    OBJECT_STORE_BATCH_RECOVERY_REQUIRED = 2
};

struct object_store {
    struct object_store_io io;
    struct object_table table;
    struct object_table_entry *scratch_entries;
    size_t table_capacity;
    uint8_t *cache_buffer;
    size_t cache_capacity;
    uint8_t *arena_buffer;
    size_t arena_capacity;
    size_t arena_used;
    struct object_cache_slot cache_slots[OBJECT_STORE_CACHE_SLOTS];
    size_t cache_slot_count;
    uint64_t cache_clock;
    uint8_t *bitmap_buffer;
    size_t bitmap_capacity;
    uint64_t next_sector;
    uint64_t current_table_sector;
    uint64_t current_generation;
    uint64_t superblock_sequence;
    uint64_t active_superblock_sector;
    uint32_t recovery_required;
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
    size_t bitmap_capacity,
    uint8_t *arena_buffer,
    size_t arena_capacity
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
    size_t bitmap_capacity,
    uint8_t *arena_buffer,
    size_t arena_capacity
);

/*
 * Publishes every request through one object-table generation. A
 * RECOVERY_REQUIRED result quarantines the store until object_store_mount().
 */
enum object_store_batch_result object_store_put_many(
    struct object_store *store,
    const struct object_store_put_request *requests,
    size_t request_count
);

int object_store_requires_recovery(
    const struct object_store *store
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

int object_store_delete(
    struct object_store *store,
    struct object_id id
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
