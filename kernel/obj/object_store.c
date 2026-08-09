#include <stddef.h>
#include <stdint.h>

#include "../lib/string.h"
#include "object_store.h"
#include "wal.h"

#define OBJECT_STORE_SUPERBLOCK_MAGIC UINT64_C(0x4A414E4953544F31)
#define OBJECT_STORE_FORMAT_VERSION UINT32_C(1)
#define OBJECT_STORE_SUPERBLOCK_A 0
#define OBJECT_STORE_SUPERBLOCK_B 1
#define OBJECT_STORE_WAL_SECTOR 2
#define OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR 3

struct disk_snapshot {
    uint64_t id;
    uint64_t table_sector;
    uint64_t table_count;
};

struct disk_superblock {
    uint64_t magic;
    uint32_t format_version;
    uint32_t sector_size;
    uint64_t sequence;
    uint64_t table_capacity;
    uint64_t next_sector;
    uint64_t current_table_sector;
    uint64_t current_table_count;
    uint64_t current_generation;
    struct disk_snapshot snapshots[OBJECT_STORE_SNAPSHOT_LIMIT];
    uint32_t checksum;
    uint32_t reserved;
    uint8_t padding[248];
};

struct disk_table_entry {
    uint64_t magic;
    struct object_table_entry entry;
    uint32_t checksum;
    uint32_t reserved;
    uint8_t padding[456];
};

_Static_assert(
    sizeof(struct disk_superblock) == OBJECT_STORE_SECTOR_SIZE,
    "object-store superblocks must occupy exactly one sector"
);

_Static_assert(
    sizeof(struct disk_table_entry) == OBJECT_STORE_SECTOR_SIZE,
    "object-table entries must occupy exactly one sector"
);

#define OBJECT_STORE_TABLE_ENTRY_MAGIC UINT64_C(0x4A414E4954424C31)



static uint64_t bitmap_bytes_for(uint64_t sector_count) {
    return (sector_count + 7u) / 8u;
}

static int bitmap_test(const struct object_store *store, uint64_t sector) {
    size_t byte_index;
    uint8_t mask;

    if (sector >= store->io.sector_count) {
        return 1;
    }

    byte_index = (size_t)(sector / 8u);
    mask = (uint8_t)(1u << (sector % 8u));
    return (store->bitmap_buffer[byte_index] & mask) != 0;
}

static void bitmap_set(struct object_store *store, uint64_t sector) {
    size_t byte_index;
    uint8_t mask;

    if (sector >= store->io.sector_count) {
        return;
    }

    byte_index = (size_t)(sector / 8u);
    mask = (uint8_t)(1u << (sector % 8u));
    store->bitmap_buffer[byte_index] |= mask;
}

static void bitmap_clear(struct object_store *store, uint64_t sector) {
    size_t byte_index;
    uint8_t mask;

    if (sector >= store->io.sector_count) {
        return;
    }

    byte_index = (size_t)(sector / 8u);
    mask = (uint8_t)(1u << (sector % 8u));
    store->bitmap_buffer[byte_index] &= (uint8_t)~mask;
}

static void bitmap_set_range(
    struct object_store *store,
    uint64_t first_sector,
    uint64_t count
) {
    uint64_t index;

    for (index = 0; index < count; index++) {
        bitmap_set(store, first_sector + index);
    }
}

static void bitmap_clear_range(
    struct object_store *store,
    uint64_t first_sector,
    uint64_t count
) {
    uint64_t index;

    for (index = 0; index < count; index++) {
        bitmap_clear(store, first_sector + index);
    }
}

static int bitmap_allocate(
    struct object_store *store,
    uint64_t count,
    uint64_t *first_sector_out
) {
    uint64_t sector;
    uint64_t run;

    if ((count == 0) || (count > store->io.sector_count)) {
        return 0;
    }

    run = 0;
    for (sector = OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR;
         sector < store->io.sector_count; sector++) {
        if (bitmap_test(store, sector)) {
            run = 0;
            continue;
        }

        run++;
        if (run == count) {
            *first_sector_out = (sector - count) + 1;
            bitmap_set_range(store, *first_sector_out, count);
            return 1;
        }
    }

    return 0;
}

static int setup_store(
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
) {
    if ((store == NULL) || (table_entries == NULL) ||
        (scratch_entries == NULL) || (cache_buffer == NULL) ||
        (bitmap_buffer == NULL) || (arena_buffer == NULL) ||
        (table_capacity == 0) ||
        (cache_capacity < OBJECT_HEADER_SIZE) ||
        (bitmap_capacity < bitmap_bytes_for(io.sector_count)) ||
        (((uintptr_t)cache_buffer % _Alignof(struct object_header)) != 0) ||
        (((uintptr_t)arena_buffer % _Alignof(struct object_header)) != 0) ||
        (io.sector_count < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
        (io.read_sector == NULL) || (io.write_sector == NULL) ||
        (io.flush == NULL)) {
        return 0;
    }

    memset(store, 0, sizeof(*store));
    store->io = io;
    object_table_init(&store->table, table_entries, table_capacity);
    store->scratch_entries = scratch_entries;
    store->table_capacity = table_capacity;
    store->cache_buffer = cache_buffer;
    store->cache_capacity = cache_capacity;
    store->bitmap_buffer = bitmap_buffer;
    store->bitmap_capacity = bitmap_capacity;
    store->arena_buffer = arena_buffer;
    store->arena_capacity = arena_capacity;
    return 1;
}

static void cache_remove_slot(struct object_store *store, size_t index) {
    size_t hole_offset;
    size_t hole_length;
    size_t i;

    hole_offset = store->cache_slots[index].offset;
    hole_length = store->cache_slots[index].length;

    memmove(store->arena_buffer + hole_offset,
            store->arena_buffer + hole_offset + hole_length,
            store->arena_used - (hole_offset + hole_length));
    store->arena_used -= hole_length;

    for (i = 0; i < store->cache_slot_count; i++) {
        if (store->cache_slots[i].offset > hole_offset) {
            store->cache_slots[i].offset -= hole_length;
        }
    }

    store->cache_slots[index] =
        store->cache_slots[store->cache_slot_count - 1];
    store->cache_slot_count--;
}

static void cache_evict_lru(struct object_store *store) {
    size_t victim;
    size_t i;

    if (store->cache_slot_count == 0) {
        return;
    }

    victim = 0;
    for (i = 1; i < store->cache_slot_count; i++) {
        if (store->cache_slots[i].last_access <
            store->cache_slots[victim].last_access) {
            victim = i;
        }
    }

    cache_remove_slot(store, victim);
}

static struct object_cache_slot *cache_find(
    struct object_store *store,
    struct object_id id,
    uint64_t version
) {
    size_t i;

    for (i = 0; i < store->cache_slot_count; i++) {
        if ((store->cache_slots[i].version == version) &&
            object_id_equal(store->cache_slots[i].id, id)) {
            return &store->cache_slots[i];
        }
    }

    return NULL;
}

static uint8_t *cache_insert(
    struct object_store *store,
    struct object_id id,
    uint64_t version,
    const uint8_t *bytes,
    size_t length
) {
    struct object_cache_slot *slot;
    uint8_t *result;
    size_t span;
    size_t i;

    span = (length + (_Alignof(struct object_header) - 1u)) &
           ~(size_t)(_Alignof(struct object_header) - 1u);

    if ((length == 0) || (span > store->arena_capacity)) {
        return NULL;
    }

    for (i = 0; i < store->cache_slot_count; i++) {
        if (object_id_equal(store->cache_slots[i].id, id)) {
            cache_remove_slot(store, i);
            break;
        }
    }

    while (store->cache_slot_count >= OBJECT_STORE_CACHE_SLOTS) {
        cache_evict_lru(store);
    }

    while ((store->arena_used + span) > store->arena_capacity) {
        cache_evict_lru(store);
    }

    result = store->arena_buffer + store->arena_used;
    memcpy(result, bytes, length);

    slot = &store->cache_slots[store->cache_slot_count];
    slot->id = id;
    slot->version = version;
    slot->offset = store->arena_used;
    slot->length = span;
    slot->last_access = ++store->cache_clock;

    store->arena_used += span;
    store->cache_slot_count++;
    return result;
}

static uint32_t superblock_checksum(struct disk_superblock *superblock) {
    uint32_t checksum;

    checksum = superblock->checksum;
    superblock->checksum = 0;
    checksum = object_crc32c((const uint8_t *)superblock,
                             sizeof(*superblock));
    superblock->checksum = checksum;
    return checksum;
}

static int superblock_is_valid(
    struct disk_superblock *superblock,
    uint64_t sector_count,
    size_t expected_capacity
) {
    uint32_t recorded_checksum;
    uint32_t calculated_checksum;

    if ((superblock->magic != OBJECT_STORE_SUPERBLOCK_MAGIC) ||
        (superblock->format_version != OBJECT_STORE_FORMAT_VERSION) ||
        (superblock->sector_size != OBJECT_STORE_SECTOR_SIZE) ||
        (superblock->table_capacity != expected_capacity) ||
        (superblock->reserved != 0) ||
        (superblock->next_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
        (superblock->next_sector > sector_count)) {
        return 0;
    }

    if (((superblock->current_table_sector == 0) &&
         (superblock->current_table_count != 0)) ||
        (superblock->current_table_count > expected_capacity)) {
        return 0;
    }

    if ((superblock->current_table_sector != 0) &&
        ((superblock->current_table_sector <
          OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
         (superblock->current_table_sector > superblock->next_sector) ||
         (superblock->current_table_count >
          (superblock->next_sector - superblock->current_table_sector)))) {
        return 0;
    }

    recorded_checksum = superblock->checksum;
    calculated_checksum = superblock_checksum(superblock);
    superblock->checksum = recorded_checksum;

    return recorded_checksum == calculated_checksum;
}

static void superblock_from_store(
    struct disk_superblock *superblock,
    const struct object_store *store,
    uint64_t sequence
) {
    size_t index;

    memset(superblock, 0, sizeof(*superblock));
    superblock->magic = OBJECT_STORE_SUPERBLOCK_MAGIC;
    superblock->format_version = OBJECT_STORE_FORMAT_VERSION;
    superblock->sector_size = OBJECT_STORE_SECTOR_SIZE;
    superblock->sequence = sequence;
    superblock->table_capacity = store->table_capacity;
    superblock->next_sector = store->next_sector;
    superblock->current_table_sector = store->current_table_sector;
    superblock->current_table_count = store->table.count;
    superblock->current_generation = store->current_generation;

    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        superblock->snapshots[index].id = store->snapshots[index].id;
        superblock->snapshots[index].table_sector =
            store->snapshots[index].table_sector;
        superblock->snapshots[index].table_count =
            store->snapshots[index].table_count;
    }

    (void)superblock_checksum(superblock);
}

static int write_superblock(struct object_store *store) {
    struct disk_superblock superblock;
    uint64_t target_sector;
    uint64_t next_sequence;

    next_sequence = store->superblock_sequence + 1;
    target_sector = (store->active_superblock_sector ==
                     OBJECT_STORE_SUPERBLOCK_A) ?
                    OBJECT_STORE_SUPERBLOCK_B : OBJECT_STORE_SUPERBLOCK_A;
    superblock_from_store(&superblock, store, next_sequence);

    if (!store->io.write_sector(store->io.context, target_sector,
                                (const uint8_t *)&superblock) ||
        !store->io.flush(store->io.context)) {
        return 0;
    }

    store->superblock_sequence = next_sequence;
    store->active_superblock_sector = target_sector;
    return 1;
}

static uint32_t table_entry_checksum(struct disk_table_entry *record) {
    uint32_t checksum;

    checksum = record->checksum;
    record->checksum = 0;
    checksum = object_crc32c((const uint8_t *)record, sizeof(*record));
    record->checksum = checksum;
    return checksum;
}

static int table_entry_is_valid(struct disk_table_entry *record) {
    uint32_t recorded_checksum;
    uint32_t calculated_checksum;

    if ((record->magic != OBJECT_STORE_TABLE_ENTRY_MAGIC) ||
        (record->reserved != 0) ||
        object_id_is_zero(record->entry.id) ||
        (record->entry.version == 0) ||
        (record->entry.first_sector == 0) ||
        (record->entry.sector_count == 0)) {
        return 0;
    }

    recorded_checksum = record->checksum;
    calculated_checksum = table_entry_checksum(record);
    record->checksum = recorded_checksum;
    return recorded_checksum == calculated_checksum;
}

static int write_table_generation(
    struct object_store *store,
    const struct object_table *table,
    uint64_t first_sector
) {
    size_t index;

    if ((table == NULL) || (table->count == 0) ||
        (table->count > table->capacity) ||
        (first_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
        (table->count > (store->io.sector_count - first_sector))) {
        return 0;
    }

    for (index = 0; index < table->count; index++) {
        struct disk_table_entry record;

        memset(&record, 0, sizeof(record));
        record.magic = OBJECT_STORE_TABLE_ENTRY_MAGIC;
        record.entry = table->entries[index];
        (void)table_entry_checksum(&record);

        if (!store->io.write_sector(store->io.context, first_sector + index,
                                    (const uint8_t *)&record)) {
            return 0;
        }
    }

    return store->io.flush(store->io.context);
}

static int write_object_bytes(
    struct object_store *store,
    uint64_t first_sector,
    const uint8_t *bytes,
    size_t byte_count
) {
    size_t offset;
    uint64_t sector;

    if ((bytes == NULL) || (byte_count == 0)) {
        return 0;
    }

    offset = 0;
    sector = first_sector;
    while (offset < byte_count) {
        uint8_t sector_bytes[OBJECT_STORE_SECTOR_SIZE];
        size_t remaining;
        size_t chunk;

        memset(sector_bytes, 0, sizeof(sector_bytes));
        remaining = byte_count - offset;
        chunk = (remaining < sizeof(sector_bytes)) ? remaining :
                sizeof(sector_bytes);
        memcpy(sector_bytes, bytes + offset, chunk);

        if (!store->io.write_sector(store->io.context, sector, sector_bytes)) {
            return 0;
        }

        offset += chunk;
        sector++;
    }

    return store->io.flush(store->io.context);
}

static int write_wal_root(
    struct object_store *store,
    uint64_t table_sector,
    uint64_t table_count,
    uint64_t generation
) {
    struct object_wal_record record;

    memset(&record, 0, sizeof(record));
    record.magic = OBJECT_WAL_MAGIC;
    record.format_version = OBJECT_WAL_FORMAT_VERSION;
    record.table_sector = table_sector;
    record.table_count = table_count;
    record.generation = generation;
    record.checksum = object_crc32c((const uint8_t *)&record,
                                    sizeof(record));

    if (!object_wal_validate((const uint8_t *)&record, sizeof(record)) ||
        !store->io.write_sector(store->io.context, OBJECT_STORE_WAL_SECTOR,
                                (const uint8_t *)&record)) {
        return 0;
    }

    return store->io.flush(store->io.context);
}

static int clear_wal(struct object_store *store) {
    uint8_t empty_sector[OBJECT_STORE_SECTOR_SIZE];

    memset(empty_sector, 0, sizeof(empty_sector));
    if (!store->io.write_sector(store->io.context, OBJECT_STORE_WAL_SECTOR,
                                empty_sector)) {
        return 0;
    }

    return store->io.flush(store->io.context);
}

static int read_table_generation(
    struct object_store *store,
    struct object_table *table,
    uint64_t first_sector,
    size_t count
) {
    size_t index;

    if ((count > table->capacity) || (count > store->table_capacity) ||
        ((count != 0) &&
         ((first_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
          (count > (store->io.sector_count - first_sector))))) {
        return 0;
    }

    table->count = 0;
    for (index = 0; index < count; index++) {
        struct disk_table_entry record;

        if (!store->io.read_sector(store->io.context, first_sector + index,
                                   (uint8_t *)&record) ||
            !table_entry_is_valid(&record) ||
            ((index != 0) &&
             (object_id_compare(table->entries[index - 1].id,
                                record.entry.id) >= 0))) {
            table->count = 0;
            return 0;
        }

        table->entries[index] = record.entry;
        table->count++;
    }

    return 1;
}

static int load_table_generation(
    struct object_store *store,
    uint64_t first_sector,
    size_t count
) {
    return read_table_generation(store, &store->table, first_sector, count);
}

static int sector_is_zero(const uint8_t *sector) {
    size_t index;

    for (index = 0; index < OBJECT_STORE_SECTOR_SIZE; index++) {
        if (sector[index] != 0) {
            return 0;
        }
    }

    return 1;
}

static int recover_wal(struct object_store *store, int *recovered) {
    struct object_wal_record record;

    *recovered = 0;

    if (!store->io.read_sector(store->io.context, OBJECT_STORE_WAL_SECTOR,
                               (uint8_t *)&record)) {
        return 0;
    }

    if (sector_is_zero((const uint8_t *)&record)) {
        return 1;
    }

    if (!object_wal_validate((const uint8_t *)&record, sizeof(record)) ||
        (record.table_count > store->table_capacity) ||
        ((record.table_count == 0) && (record.table_sector != 0)) ||
        ((record.table_count != 0) &&
         ((record.table_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
          (record.table_sector > store->next_sector) ||
          (record.table_count >
           (store->next_sector - record.table_sector))))) {
        return 0;
    }

    if (!load_table_generation(store, record.table_sector,
                               (size_t)record.table_count)) {
        return 0;
    }

    store->current_table_sector = record.table_sector;
    store->current_generation = record.generation;
    if (!write_superblock(store) || !clear_wal(store)) {
        return 0;
    }

    *recovered = 1;
    return 1;
}

static int mark_generation(
    struct object_store *store,
    uint64_t table_sector,
    uint64_t table_count
) {
    struct object_table generation;
    size_t entry_index;

    if (table_count == 0) {
        return 1;
    }

    object_table_init(&generation, store->scratch_entries,
                      store->table_capacity);
    if (!read_table_generation(store, &generation, table_sector,
                               (size_t)table_count)) {
        return 0;
    }

    bitmap_set_range(store, table_sector, table_count);

    for (entry_index = 0; entry_index < generation.count; entry_index++) {
        bitmap_set_range(store,
                         generation.entries[entry_index].first_sector,
                         generation.entries[entry_index].sector_count);
    }

    return 1;
}

static int rebuild_bitmap(struct object_store *store) {
    uint64_t needed;
    size_t slot;

    needed = bitmap_bytes_for(store->io.sector_count);
    if (needed > store->bitmap_capacity) {
        return 0;
    }

    memset(store->bitmap_buffer, 0, (size_t)needed);
    bitmap_set_range(store, 0, OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR);

    if (!mark_generation(store, store->current_table_sector,
                         store->table.count)) {
        return 0;
    }

    for (slot = 0; slot < OBJECT_STORE_SNAPSHOT_LIMIT; slot++) {
        if (store->snapshots[slot].id == 0) {
            continue;
        }
        if (!mark_generation(store, store->snapshots[slot].table_sector,
                             store->snapshots[slot].table_count)) {
            return 0;
        }
    }

    return 1;
}

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
) {
    uint8_t empty_sector[OBJECT_STORE_SECTOR_SIZE];

    if (!setup_store(store, io, table_entries, scratch_entries,
                     table_capacity, cache_buffer, cache_capacity,
                     bitmap_buffer, bitmap_capacity,
                     arena_buffer, arena_capacity)) {
        return 0;
    }

    store->next_sector = OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR;
    store->active_superblock_sector = OBJECT_STORE_SUPERBLOCK_A;

    memset(store->bitmap_buffer, 0,
           (size_t)bitmap_bytes_for(io.sector_count));
    bitmap_set_range(store, 0, OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR);

    if (!write_superblock(store) || !write_superblock(store)) {
        return 0;
    }

    memset(empty_sector, 0, sizeof(empty_sector));
    if (!store->io.write_sector(store->io.context, OBJECT_STORE_WAL_SECTOR,
                                empty_sector) ||
        !store->io.flush(store->io.context)) {
        return 0;
    }

    return 1;
}

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
) {
    struct disk_superblock first;
    struct disk_superblock second;
    const struct disk_superblock *chosen;
    size_t index;
    int recovered;

    if (!setup_store(store, io, table_entries, scratch_entries,
                     table_capacity, cache_buffer, cache_capacity,
                     bitmap_buffer, bitmap_capacity,
                     arena_buffer, arena_capacity) ||
        !io.read_sector(io.context, OBJECT_STORE_SUPERBLOCK_A,
                        (uint8_t *)&first) ||
        !io.read_sector(io.context, OBJECT_STORE_SUPERBLOCK_B,
                        (uint8_t *)&second)) {
        return 0;
    }

    if (superblock_is_valid(&first, io.sector_count, table_capacity) &&
        (!superblock_is_valid(&second, io.sector_count, table_capacity) ||
         (first.sequence >= second.sequence))) {
        chosen = &first;
        store->active_superblock_sector = OBJECT_STORE_SUPERBLOCK_A;
    } else if (superblock_is_valid(&second, io.sector_count, table_capacity)) {
        chosen = &second;
        store->active_superblock_sector = OBJECT_STORE_SUPERBLOCK_B;
    } else {
        return 0;
    }

    store->superblock_sequence = chosen->sequence;
    store->next_sector = chosen->next_sector;
    store->current_table_sector = chosen->current_table_sector;
    store->current_generation = chosen->current_generation;

    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        store->snapshots[index].id = chosen->snapshots[index].id;
        store->snapshots[index].table_sector = chosen->snapshots[index].table_sector;
        store->snapshots[index].table_count = chosen->snapshots[index].table_count;
    }

    if (!recover_wal(store, &recovered)) {
        return 0;
    }

    if (!recovered &&
        !load_table_generation(store, chosen->current_table_sector,
                               (size_t)chosen->current_table_count)) {
        return 0;
    }

    if (!rebuild_bitmap(store)) {
        return 0;
    }

    return 1;
}

int object_store_put(
    struct object_store *store,
    struct object_id id,
    struct object_id type_id,
    struct object_id creator_id,
    struct object_id modifier_id,
    uint64_t logical_timestamp,
    const uint8_t *payload,
    size_t payload_size
) {
    struct object_table candidate;
    struct object_table_entry entry;
    const struct object_table_entry *existing;
    struct object_header *header;
    uint64_t data_sectors;
    uint64_t data_sector;
    uint64_t table_sector;
    uint64_t generation;
    size_t object_byte_count;
    size_t index;

    if ((store == NULL) || object_id_is_zero(id) ||
        object_id_is_zero(type_id) ||
        ((payload == NULL) && (payload_size != 0)) ||
        (payload_size > (SIZE_MAX - OBJECT_HEADER_SIZE)) ||
        (payload_size > (store->cache_capacity - OBJECT_HEADER_SIZE))) {
        return 0;
    }

    object_byte_count = OBJECT_HEADER_SIZE + payload_size;
    data_sectors = (object_byte_count + (OBJECT_STORE_SECTOR_SIZE - 1)) /
                   OBJECT_STORE_SECTOR_SIZE;
    if ((data_sectors == 0) || (store->current_generation == UINT64_MAX)) {
        return 0;
    }

    object_table_init(&candidate, store->scratch_entries, store->table_capacity);
    memcpy(candidate.entries, store->table.entries,
           store->table.count * sizeof(*candidate.entries));
    candidate.count = store->table.count;

    existing = object_table_find(&store->table, id);
    memset(&entry, 0, sizeof(entry));
    entry.id = id;
    entry.version = (existing == NULL) ? 1 : existing->version + 1;
    if (entry.version == 0) {
        return 0;
    }
    entry.first_sector = OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR;
    entry.sector_count = data_sectors;

    if (!object_table_upsert(&candidate, entry) ||
        (candidate.count > UINT64_MAX) ||
        (data_sectors > (UINT64_MAX - candidate.count))) {
        return 0;
    }

    if (!bitmap_allocate(store, data_sectors, &data_sector)) {
        return 0;
    }
    if (!bitmap_allocate(store, candidate.count, &table_sector)) {
        bitmap_clear_range(store, data_sector, data_sectors);
        return 0;
    }

    if ((data_sector + data_sectors) > store->next_sector) {
        store->next_sector = data_sector + data_sectors;
    }
    if ((table_sector + candidate.count) > store->next_sector) {
        store->next_sector = table_sector + candidate.count;
    }
    for (index = 0; index < candidate.count; index++) {
        if (object_id_equal(candidate.entries[index].id, id)) {
            candidate.entries[index].first_sector = data_sector;
            break;
        }
    }
    if (index == candidate.count) {
        return 0;
    }

    memset(store->cache_buffer, 0, object_byte_count);
    header = (struct object_header *)store->cache_buffer;
    header->magic = OBJECT_HEADER_MAGIC;
    header->format_version = OBJECT_HEADER_FORMAT_VERSION;
    header->id = id;
    header->type_id = type_id;
    header->version = entry.version;
    header->payload_size = payload_size;
    header->creator_id = creator_id;
    header->modifier_id = modifier_id;
    header->logical_timestamp = logical_timestamp;
    if (payload_size != 0) {
        memcpy(store->cache_buffer + OBJECT_HEADER_SIZE, payload, payload_size);
    }
    header->payload_crc32c = object_crc32c(
        store->cache_buffer + OBJECT_HEADER_SIZE, payload_size
    );
    if (!object_header_validate(store->cache_buffer, object_byte_count)) {
        return 0;
    }

    if (!write_superblock(store) ||
        !write_object_bytes(store, data_sector, store->cache_buffer,
                            object_byte_count) ||
        !write_table_generation(store, &candidate, table_sector)) {
        bitmap_clear_range(store, data_sector, data_sectors);
        bitmap_clear_range(store, table_sector, candidate.count);
        return 0;
    }

    generation = store->current_generation + 1;
    if (!write_wal_root(store, table_sector, candidate.count, generation)) {
        return 0;
    }

    memcpy(store->table.entries, candidate.entries,
           candidate.count * sizeof(*candidate.entries));
    store->table.count = candidate.count;
    store->current_table_sector = table_sector;
    store->current_generation = generation;
    if (!write_superblock(store) || !clear_wal(store)) {
        return 0;
    }

    (void)cache_insert(store, id, entry.version, store->cache_buffer,
                       object_byte_count);
    return 1;
}

int object_store_get(
    struct object_store *store,
    struct object_id id,
    struct object_header *header_out,
    const uint8_t **payload_out,
    size_t *payload_size_out
) {
    const struct object_table_entry *entry;
    const struct object_header *header;
    struct object_cache_slot *slot;
    const uint8_t *object_bytes;
    uint8_t *cached;
    size_t object_byte_count;
    size_t sector_count;
    size_t index;

    if ((store == NULL) || object_id_is_zero(id) || (header_out == NULL) ||
        (payload_out == NULL) || (payload_size_out == NULL)) {
        return 0;
    }

    entry = object_table_find(&store->table, id);
    if (entry == NULL) {
        return 0;
    }

    slot = cache_find(store, id, entry->version);
    if (slot != NULL) {
        object_bytes = store->arena_buffer + slot->offset;
        slot->last_access = ++store->cache_clock;
    } else {
        if ((entry->sector_count == 0) ||
            (entry->first_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
            (entry->first_sector >= store->io.sector_count) ||
            (entry->sector_count >
             (store->io.sector_count - entry->first_sector)) ||
            (entry->sector_count >
             (store->cache_capacity / OBJECT_STORE_SECTOR_SIZE))) {
            return 0;
        }

        sector_count = (size_t)entry->sector_count;
        object_byte_count = sector_count * OBJECT_STORE_SECTOR_SIZE;

        for (index = 0; index < sector_count; index++) {
            if (!store->io.read_sector(
                    store->io.context,
                    entry->first_sector + index,
                    store->cache_buffer +
                    (index * OBJECT_STORE_SECTOR_SIZE))) {
                return 0;
            }
        }

        if (!object_header_validate(store->cache_buffer, object_byte_count)) {
            return 0;
        }

        header = (const struct object_header *)store->cache_buffer;
        if (!object_id_equal(header->id, id) ||
            (header->version != entry->version)) {
            return 0;
        }

        object_bytes = store->cache_buffer;
        cached = cache_insert(store, id, entry->version, store->cache_buffer,
                              object_byte_count);
        if (cached != NULL) {
            object_bytes = cached;
        }
    }

    header = (const struct object_header *)object_bytes;

    *header_out = *header;
    *payload_out = object_bytes + OBJECT_HEADER_SIZE;
    *payload_size_out = (size_t)header->payload_size;
    return 1;
}

int object_store_delete(
    struct object_store *store,
    struct object_id id
) {
    struct object_table candidate;
    const struct object_table_entry *existing;
    uint64_t table_sector;
    uint64_t generation;
    size_t index;

    if ((store == NULL) || object_id_is_zero(id) ||
        (store->current_generation == UINT64_MAX)) {
        return 0;
    }

    existing = object_table_find(&store->table, id);
    if (existing == NULL) {
        return 0;
    }

    object_table_init(&candidate, store->scratch_entries,
                      store->table_capacity);
    memcpy(candidate.entries, store->table.entries,
           store->table.count * sizeof(*candidate.entries));
    candidate.count = store->table.count;

    if (!object_table_remove(&candidate, id)) {
        return 0;
    }

    table_sector = 0;
    if ((candidate.count != 0) &&
        !bitmap_allocate(store, candidate.count, &table_sector)) {
        return 0;
    }

    if ((candidate.count != 0) &&
        ((table_sector + candidate.count) > store->next_sector)) {
        store->next_sector = table_sector + candidate.count;
    }

    if (!write_superblock(store) ||
        ((candidate.count != 0) &&
         !write_table_generation(store, &candidate, table_sector))) {
        if (candidate.count != 0) {
            bitmap_clear_range(store, table_sector, candidate.count);
        }
        return 0;
    }

    generation = store->current_generation + 1;
    if (!write_wal_root(store, table_sector, candidate.count, generation)) {
        return 0;
    }

    memcpy(store->table.entries, candidate.entries,
           candidate.count * sizeof(*candidate.entries));
    store->table.count = candidate.count;
    store->current_table_sector = table_sector;
    store->current_generation = generation;
    if (!write_superblock(store) || !clear_wal(store)) {
        return 0;
    }

    for (index = 0; index < store->cache_slot_count; index++) {
        if (object_id_equal(store->cache_slots[index].id, id)) {
            cache_remove_slot(store, index);
            break;
        }
    }

    return 1;
}

int object_store_snapshot_create(
    struct object_store *store,
    uint64_t *snapshot_id_out
) {
    struct object_store_snapshot saved[OBJECT_STORE_SNAPSHOT_LIMIT];
    uint64_t snapshot_id;
    uint64_t saved_generation;
    size_t slot;
    size_t index;

    if ((store == NULL) || (snapshot_id_out == NULL) ||
        (store->current_generation == UINT64_MAX) ||
        (store->current_table_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
        (store->table.count == 0)) {
        return 0;
    }

    snapshot_id = store->current_generation + 1;

    slot = OBJECT_STORE_SNAPSHOT_LIMIT;
    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        if (store->snapshots[index].id == snapshot_id) {
            return 0;
        }
        if ((slot == OBJECT_STORE_SNAPSHOT_LIMIT) &&
            (store->snapshots[index].id == 0)) {
            slot = index;
        }
    }
    if (slot == OBJECT_STORE_SNAPSHOT_LIMIT) {
        return 0;
    }

    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        saved[index] = store->snapshots[index];
    }
    saved_generation = store->current_generation;

    store->snapshots[slot].id = snapshot_id;
    store->snapshots[slot].table_sector = store->current_table_sector;
    store->snapshots[slot].table_count = store->table.count;
    store->current_generation = snapshot_id;

    if (!write_superblock(store)) {
        for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
            store->snapshots[index] = saved[index];
        }
        store->current_generation = saved_generation;
        return 0;
    }

    *snapshot_id_out = snapshot_id;
    return 1;
}

int object_store_snapshot_rollback(
    struct object_store *store,
    uint64_t snapshot_id
) {
    struct object_table candidate;
    uint64_t table_sector;
    uint64_t table_count;
    uint64_t generation;
    size_t slot;

    if ((store == NULL) || (snapshot_id == 0) ||
        (store->current_generation == UINT64_MAX)) {
        return 0;
    }

    for (slot = 0; slot < OBJECT_STORE_SNAPSHOT_LIMIT; slot++) {
        if (store->snapshots[slot].id == snapshot_id) {
            break;
        }
    }
    if (slot == OBJECT_STORE_SNAPSHOT_LIMIT) {
        return 0;
    }

    table_sector = store->snapshots[slot].table_sector;
    table_count = store->snapshots[slot].table_count;
    if ((table_sector < OBJECT_STORE_FIRST_ALLOCATABLE_SECTOR) ||
        (table_count == 0) || (table_count > store->table_capacity)) {
        return 0;
    }

    object_table_init(&candidate, store->scratch_entries,
                      store->table_capacity);
    if (!read_table_generation(store, &candidate, table_sector,
                               (size_t)table_count)) {
        return 0;
    }

    generation = store->current_generation + 1;
    if (!write_wal_root(store, table_sector, table_count, generation)) {
        return 0;
    }

    memcpy(store->table.entries, candidate.entries,
           candidate.count * sizeof(*store->table.entries));
    store->table.count = candidate.count;
    store->current_table_sector = table_sector;
    store->current_generation = generation;

    if (!write_superblock(store) || !clear_wal(store)) {
        return 0;
    }

    return 1;
}

int object_store_snapshot_discard(
    struct object_store *store,
    uint64_t snapshot_id
) {
    struct object_store_snapshot saved;
    size_t slot;

    if ((store == NULL) || (snapshot_id == 0)) {
        return 0;
    }

    for (slot = 0; slot < OBJECT_STORE_SNAPSHOT_LIMIT; slot++) {
        if (store->snapshots[slot].id == snapshot_id) {
            break;
        }
    }
    if (slot == OBJECT_STORE_SNAPSHOT_LIMIT) {
        return 0;
    }

    saved = store->snapshots[slot];
    store->snapshots[slot].id = 0;
    store->snapshots[slot].table_sector = 0;
    store->snapshots[slot].table_count = 0;

    if (!write_superblock(store)) {
        store->snapshots[slot] = saved;
        return 0;
    }

    return 1;
}

int object_store_collect(struct object_store *store) {
    if (store == NULL) {
        return 0;
    }

    return rebuild_bitmap(store);
}

int object_store_snapshot_prune(struct object_store *store, size_t keep) {
    uint64_t oldest_id;
    size_t used;
    size_t slot;
    size_t oldest_slot;

    if ((store == NULL) || (keep > OBJECT_STORE_SNAPSHOT_LIMIT)) {
        return 0;
    }

    for (;;) {
        used = 0;
        oldest_id = 0;
        oldest_slot = OBJECT_STORE_SNAPSHOT_LIMIT;

        for (slot = 0; slot < OBJECT_STORE_SNAPSHOT_LIMIT; slot++) {
            if (store->snapshots[slot].id == 0) {
                continue;
            }
            used++;
            if ((oldest_id == 0) ||
                (store->snapshots[slot].id < oldest_id)) {
                oldest_id = store->snapshots[slot].id;
                oldest_slot = slot;
            }
        }

        if ((used <= keep) || (oldest_slot == OBJECT_STORE_SNAPSHOT_LIMIT)) {
            break;
        }

        if (!object_store_snapshot_discard(store, oldest_id)) {
            return 0;
        }
    }

    return object_store_collect(store);
}

