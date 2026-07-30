#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_store.h"
#include "check.h"

#define DISK_SECTORS 128u
#define TABLE_CAPACITY 8u
#define CACHE_BYTES 4096u
#define BITMAP_BYTES ((DISK_SECTORS + 7u) / 8u)
#define ARENA_BYTES 8192u

#define PRESSURE_SECTORS 512u
#define PRESSURE_TABLE 24u
#define PRESSURE_BITMAP_BYTES ((PRESSURE_SECTORS + 7u) / 8u)
#define PRESSURE_CACHE_BYTES 8192u
#define PRESSURE_OBJECTS 20u
#define PRESSURE_ROOMY_ARENA \
    (PRESSURE_OBJECTS * OBJECT_STORE_SECTOR_SIZE)

unsigned long checks_passed;

struct hosted_disk {
    uint8_t bytes[DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    unsigned long writes_allowed;
};

struct pressure_disk {
    uint8_t bytes[PRESSURE_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    unsigned long writes_allowed;
};

static int disk_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct hosted_disk *disk = context;

    if ((sector >= DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    memcpy(buffer, disk->bytes[sector], OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int disk_write(
    void *context,
    uint64_t sector,
    const uint8_t *buffer
) {
    struct hosted_disk *disk = context;

    if ((sector >= DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    if (disk->writes_allowed == 0) {
        return 0;
    }
    disk->writes_allowed--;

    memcpy(disk->bytes[sector], buffer, OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int disk_flush(void *context) {
    return context != NULL;
}

static struct object_store_io make_io(struct hosted_disk *disk) {
    struct object_store_io io;

    io.context = disk;
    io.sector_count = DISK_SECTORS;
    io.read_sector = disk_read;
    io.write_sector = disk_write;
    io.flush = disk_flush;
    return io;
}

static int bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }

    return 1;
}

static struct object_id make_id(uint64_t high, uint64_t low) {
    struct object_id id;

    id.high = high;
    id.low = low;
    return id;
}

static void test_commit_and_remount(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store remounted;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry remounted_entries[TABLE_CAPACITY];
    struct object_table_entry remounted_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
    uint8_t remounted_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t remounted_arena[ARENA_BYTES];
    const struct object_table_entry *entry;
    struct object_id id;
    struct object_id type_id;
    const uint8_t first_payload[] = { 1, 2, 3 };
    const uint8_t second_payload[] = { 4, 5, 6, 7 };
    uint64_t first_sector;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    id = make_id(1, 1);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, first_payload,
                           sizeof(first_payload)));
    CHECK(store.table.count == 1);
    entry = object_table_find(&store.table, id);
    CHECK(entry != NULL);
    CHECK(entry->version == 1);
    first_sector = entry->first_sector;

    CHECK(object_store_mount(&remounted, make_io(&disk), remounted_entries,
                             remounted_scratch, TABLE_CAPACITY,
                             remounted_cache, sizeof(remounted_cache),
                             remounted_bitmap, sizeof(remounted_bitmap),
                             remounted_arena, sizeof(remounted_arena)));
    entry = object_table_find(&remounted.table, id);
    CHECK(entry != NULL);
    CHECK(entry->version == 1);
    CHECK(entry->first_sector == first_sector);

    CHECK(object_store_put(&remounted, id, type_id, make_id(3, 1),
                           make_id(4, 1), 2, second_payload,
                           sizeof(second_payload)));
    entry = object_table_find(&remounted.table, id);
    CHECK(entry != NULL);
    CHECK(entry->version == 2);
    CHECK(entry->first_sector != first_sector);
}

static void test_crash_after_wal_recovers(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store recovered;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry recovered_entries[TABLE_CAPACITY];
    struct object_table_entry recovered_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t recovered_cache[CACHE_BYTES];
    uint8_t recovered_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t recovered_arena[ARENA_BYTES];
    const struct object_table_entry *entry;
    struct object_id first_id;
    struct object_id second_id;
    struct object_id type_id;
    const uint8_t payload[] = { 9, 8, 7 };

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    first_id = make_id(1, 1);
    second_id = make_id(1, 2);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, first_id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    CHECK(store.table.count == 1);

    disk.writes_allowed = 5;
    CHECK(!object_store_put(&store, second_id, type_id, make_id(3, 1),
                            make_id(3, 1), 2, payload, sizeof(payload)));
    CHECK(disk.writes_allowed == 0);

    disk.writes_allowed = ULONG_MAX;
    CHECK(object_store_mount(&recovered, make_io(&disk), recovered_entries,
                             recovered_scratch, TABLE_CAPACITY,
                             recovered_cache, sizeof(recovered_cache),
                             recovered_bitmap, sizeof(recovered_bitmap),
                             recovered_arena, sizeof(recovered_arena)));
    CHECK(recovered.table.count == 2);
    entry = object_table_find(&recovered.table, first_id);
    CHECK(entry != NULL);
    entry = object_table_find(&recovered.table, second_id);
    CHECK(entry != NULL);
    CHECK(entry->version == 1);
}

static void test_snapshot_create_and_rollback(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store remounted;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry remounted_entries[TABLE_CAPACITY];
    struct object_table_entry remounted_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
    uint8_t remounted_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t remounted_arena[ARENA_BYTES];
    const struct object_table_entry *entry;
    struct object_id first_id;
    struct object_id second_id;
    struct object_id type_id;
    const uint8_t payload[] = { 1, 2, 3 };
    uint64_t snapshot;
    uint64_t later_snapshot;
    uint64_t table_sector;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    first_id = make_id(5, 1);
    second_id = make_id(5, 2);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));

    CHECK(!object_store_snapshot_create(&store, &snapshot));

    CHECK(object_store_put(&store, first_id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    table_sector = store.current_table_sector;

    CHECK(object_store_snapshot_create(&store, &snapshot));
    CHECK(snapshot != 0);
    CHECK(store.snapshots[0].id == snapshot);
    CHECK(store.snapshots[0].table_sector == table_sector);
    CHECK(store.snapshots[0].table_count == 1);

    CHECK(object_store_put(&store, second_id, type_id, make_id(3, 1),
                           make_id(3, 1), 2, payload, sizeof(payload)));
    CHECK(store.table.count == 2);

    CHECK(object_store_snapshot_create(&store, &later_snapshot));
    CHECK(later_snapshot != snapshot);

    CHECK(!object_store_snapshot_rollback(&store, 0));
    CHECK(!object_store_snapshot_rollback(&store, snapshot + 4242));

    CHECK(object_store_snapshot_rollback(&store, snapshot));
    CHECK(store.table.count == 1);
    CHECK(store.current_table_sector == table_sector);
    entry = object_table_find(&store.table, first_id);
    CHECK(entry != NULL);
    CHECK(object_table_find(&store.table, second_id) == NULL);
    CHECK(store.snapshots[1].id == later_snapshot);

    CHECK(object_store_mount(&remounted, make_io(&disk), remounted_entries,
                             remounted_scratch, TABLE_CAPACITY,
                             remounted_cache, sizeof(remounted_cache),
                             remounted_bitmap, sizeof(remounted_bitmap),
                             remounted_arena, sizeof(remounted_arena)));
    CHECK(remounted.table.count == 1);
    CHECK(object_table_find(&remounted.table, first_id) != NULL);
    CHECK(object_table_find(&remounted.table, second_id) == NULL);
    CHECK(remounted.snapshots[0].id == snapshot);
    CHECK(remounted.snapshots[1].id == later_snapshot);

    CHECK(object_store_snapshot_rollback(&remounted, later_snapshot));
    CHECK(remounted.table.count == 2);
    CHECK(object_table_find(&remounted.table, second_id) != NULL);
}

static void test_rollback_crash_after_wal_recovers(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store recovered;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry recovered_entries[TABLE_CAPACITY];
    struct object_table_entry recovered_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t recovered_cache[CACHE_BYTES];
    uint8_t recovered_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t recovered_arena[ARENA_BYTES];
    struct object_id first_id;
    struct object_id second_id;
    struct object_id type_id;
    const uint8_t payload[] = { 4, 5, 6 };
    uint64_t snapshot;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    first_id = make_id(7, 1);
    second_id = make_id(7, 2);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, first_id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    CHECK(object_store_snapshot_create(&store, &snapshot));
    CHECK(object_store_put(&store, second_id, type_id, make_id(3, 1),
                           make_id(3, 1), 2, payload, sizeof(payload)));
    CHECK(store.table.count == 2);

    disk.writes_allowed = 1;
    CHECK(!object_store_snapshot_rollback(&store, snapshot));
    CHECK(disk.writes_allowed == 0);

    disk.writes_allowed = ULONG_MAX;
    CHECK(object_store_mount(&recovered, make_io(&disk), recovered_entries,
                             recovered_scratch, TABLE_CAPACITY,
                             recovered_cache, sizeof(recovered_cache),
                             recovered_bitmap, sizeof(recovered_bitmap),
                             recovered_arena, sizeof(recovered_arena)));
    CHECK(recovered.table.count == 1);
    CHECK(object_table_find(&recovered.table, first_id) != NULL);
    CHECK(object_table_find(&recovered.table, second_id) == NULL);
}

static void test_get_hot_cold_and_after_rollback(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store cold;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry cold_entries[TABLE_CAPACITY];
    struct object_table_entry cold_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t cold_cache[CACHE_BYTES];
    uint8_t cold_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t cold_arena[ARENA_BYTES];
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id id;
    struct object_id type_id;
    const uint8_t first_payload[] = { 10, 20, 30 };
    const uint8_t second_payload[] = { 40, 50, 60, 70 };
    uint64_t snapshot;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    id = make_id(11, 1);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, first_payload,
                           sizeof(first_payload)));

    CHECK(object_store_get(&store, id, &header, &payload, &payload_size));
    CHECK(header.version == 1);
    CHECK(object_id_equal(header.id, id));
    CHECK(payload_size == sizeof(first_payload));
    CHECK(bytes_equal(payload, first_payload, sizeof(first_payload)));

    CHECK(!object_store_get(&store, make_id(99, 99), &header, &payload,
                            &payload_size));

    CHECK(object_store_snapshot_create(&store, &snapshot));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(4, 1), 2, second_payload,
                           sizeof(second_payload)));
    CHECK(object_store_get(&store, id, &header, &payload, &payload_size));
    CHECK(header.version == 2);
    CHECK(payload_size == sizeof(second_payload));
    CHECK(bytes_equal(payload, second_payload, sizeof(second_payload)));

    CHECK(object_store_snapshot_rollback(&store, snapshot));
    CHECK(object_store_get(&store, id, &header, &payload, &payload_size));
    CHECK(header.version == 1);
    CHECK(payload_size == sizeof(first_payload));
    CHECK(bytes_equal(payload, first_payload, sizeof(first_payload)));

    CHECK(object_store_mount(&cold, make_io(&disk), cold_entries,
                             cold_scratch, TABLE_CAPACITY, cold_cache,
                             sizeof(cold_cache), cold_bitmap,
                             sizeof(cold_bitmap), cold_arena,
                             sizeof(cold_arena)));
    CHECK(object_store_get(&cold, id, &header, &payload, &payload_size));
    CHECK(header.version == 1);
    CHECK(bytes_equal(payload, first_payload, sizeof(first_payload)));
}

static void test_snapshot_discard(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store remounted;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry remounted_entries[TABLE_CAPACITY];
    struct object_table_entry remounted_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
    uint8_t remounted_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t remounted_arena[ARENA_BYTES];
    struct object_id id;
    struct object_id type_id;
    const uint8_t payload[] = { 1, 2, 3 };
    uint64_t snapshots[OBJECT_STORE_SNAPSHOT_LIMIT];
    uint64_t overflow;
    uint64_t replacement;
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    id = make_id(13, 1);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));

    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        CHECK(object_store_snapshot_create(&store, &snapshots[index]));
    }
    CHECK(!object_store_snapshot_create(&store, &overflow));

    CHECK(!object_store_snapshot_discard(&store, 0));
    CHECK(!object_store_snapshot_discard(&store, snapshots[0] + 4242));

    CHECK(object_store_snapshot_discard(&store, snapshots[3]));
    CHECK(store.snapshots[3].id == 0);
    CHECK(store.snapshots[3].table_sector == 0);
    CHECK(store.snapshots[3].table_count == 0);
    CHECK(!object_store_snapshot_rollback(&store, snapshots[3]));

    CHECK(object_store_snapshot_create(&store, &replacement));
    CHECK(store.snapshots[3].id == replacement);

    CHECK(object_store_snapshot_discard(&store, snapshots[5]));
    CHECK(object_store_mount(&remounted, make_io(&disk), remounted_entries,
                             remounted_scratch, TABLE_CAPACITY,
                             remounted_cache, sizeof(remounted_cache),
                             remounted_bitmap, sizeof(remounted_bitmap),
                             remounted_arena, sizeof(remounted_arena)));
    CHECK(remounted.snapshots[5].id == 0);
    CHECK(remounted.snapshots[3].id == replacement);
    CHECK(remounted.snapshots[0].id == snapshots[0]);
}

static void test_collect_reclaims_freed_sectors(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    struct object_id id;
    struct object_id type_id;
    const uint8_t payload[] = { 1, 2, 3 };
    uint64_t next_after_first;
    uint64_t next_after_second;
    uint64_t snapshot;
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));

    id = make_id(21, 1);
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    next_after_first = store.next_sector;

    for (index = 0; index < 10; index++) {
        CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                               make_id(3, 1), index + 2, payload,
                               sizeof(payload)));
    }
    next_after_second = store.next_sector;
    CHECK(next_after_second > next_after_first);

    CHECK(object_store_collect(&store));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 12, payload, sizeof(payload)));
    CHECK(store.next_sector <= next_after_second);

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    id = make_id(22, 1);
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    CHECK(object_store_snapshot_create(&store, &snapshot));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 2, payload, sizeof(payload)));

    CHECK(object_store_collect(&store));
    CHECK(object_store_put(&store, make_id(22, 2), type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));
    CHECK(object_store_snapshot_rollback(&store, snapshot));
    CHECK(store.table.count == 1);
    CHECK(object_table_find(&store.table, id) != NULL);
    CHECK(object_table_find(&store.table, make_id(22, 2)) == NULL);
}

static void test_prune_keeps_newest(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    struct object_id id;
    struct object_id type_id;
    const uint8_t payload[] = { 1, 2, 3 };
    uint64_t snapshots[6];
    size_t index;
    size_t used;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    id = make_id(23, 1);
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache),
                              bitmap, sizeof(bitmap),
                              arena, sizeof(arena)));
    CHECK(object_store_put(&store, id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, payload, sizeof(payload)));

    for (index = 0; index < 6; index++) {
        CHECK(object_store_snapshot_create(&store, &snapshots[index]));
    }

    CHECK(object_store_snapshot_prune(&store, 2));

    used = 0;
    for (index = 0; index < OBJECT_STORE_SNAPSHOT_LIMIT; index++) {
        if (store.snapshots[index].id != 0) {
            used++;
        }
    }
    CHECK(used == 2);
    CHECK(!object_store_snapshot_rollback(&store, snapshots[0]));
    CHECK(object_store_snapshot_rollback(&store, snapshots[5]));
}

static int pressure_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct pressure_disk *disk = context;

    if ((sector >= PRESSURE_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    memcpy(buffer, disk->bytes[sector], OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int pressure_write(
    void *context,
    uint64_t sector,
    const uint8_t *buffer
) {
    struct pressure_disk *disk = context;

    if ((sector >= PRESSURE_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    if (disk->writes_allowed == 0) {
        return 0;
    }
    disk->writes_allowed--;

    memcpy(disk->bytes[sector], buffer, OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static struct object_store_io make_pressure_io(struct pressure_disk *disk) {
    struct object_store_io io;

    io.context = disk;
    io.sector_count = PRESSURE_SECTORS;
    io.read_sector = pressure_read;
    io.write_sector = pressure_write;
    io.flush = disk_flush;
    return io;
}

static int cache_holds(const struct object_store *store, struct object_id id) {
    size_t index;

    for (index = 0; index < store->cache_slot_count; index++) {
        if (object_id_equal(store->cache_slots[index].id, id)) {
            return 1;
        }
    }

    return 0;
}

static int arena_is_consistent(const struct object_store *store) {
    size_t total;
    size_t left;
    size_t right;

    if (store->cache_slot_count > OBJECT_STORE_CACHE_SLOTS) {
        return 0;
    }
    if (store->arena_used > store->arena_capacity) {
        return 0;
    }

    total = 0;
    for (left = 0; left < store->cache_slot_count; left++) {
        size_t left_start = store->cache_slots[left].offset;
        size_t left_end = left_start + store->cache_slots[left].length;

        if ((store->cache_slots[left].length == 0) ||
            (left_end > store->arena_used)) {
            return 0;
        }
        total += store->cache_slots[left].length;

        for (right = left + 1; right < store->cache_slot_count; right++) {
            size_t right_start = store->cache_slots[right].offset;
            size_t right_end = right_start + store->cache_slots[right].length;

            if ((left_start < right_end) && (right_start < left_end)) {
                return 0;
            }
        }
    }

    return total == store->arena_used;
}

static void fill_payload(uint8_t *buffer, size_t length, uint8_t seed) {
    size_t index;

    for (index = 0; index < length; index++) {
        buffer[index] = (uint8_t)(seed + (uint8_t)(index * 7u));
    }
}

static int region_is_zero(const uint8_t *bytes, size_t start, size_t end) {
    size_t index;

    for (index = start; index < end; index++) {
        if (bytes[index] != 0) {
            return 0;
        }
    }

    return 1;
}

static uint64_t first_sector_of(
    const struct object_store *store,
    struct object_id id
) {
    size_t index;

    for (index = 0; index < store->table.count; index++) {
        if (object_id_equal(store->table.entries[index].id, id)) {
            return store->table.entries[index].first_sector;
        }
    }

    return 0;
}

static void test_written_sectors_are_zero_padded(void) {
    static struct pressure_disk disk;
    struct object_store store;
    struct object_table_entry entries[PRESSURE_TABLE];
    struct object_table_entry scratch[PRESSURE_TABLE];
    _Alignas(16) uint8_t cache[PRESSURE_CACHE_BYTES];
    uint8_t bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t arena[PRESSURE_CACHE_BYTES];
    struct object_id wide_id;
    struct object_id narrow_id;
    struct object_id type_id;
    uint8_t wide_payload[400];
    uint8_t narrow_payload[4];
    uint64_t wide_sector;
    uint64_t narrow_sector;
    size_t wide_tail;
    size_t narrow_tail;
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    wide_id = make_id(120, 1);
    narrow_id = make_id(120, 2);
    type_id = make_id(2, 1);

    for (index = 0; index < sizeof(wide_payload); index++) {
        wide_payload[index] = 0xAA;
    }
    for (index = 0; index < sizeof(narrow_payload); index++) {
        narrow_payload[index] = 0x11;
    }

    CHECK(object_store_format(&store, make_pressure_io(&disk), entries,
                              scratch, PRESSURE_TABLE, cache, sizeof(cache),
                              bitmap, sizeof(bitmap), arena, sizeof(arena)));

    CHECK(object_store_put(&store, wide_id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, wide_payload,
                           sizeof(wide_payload)));
    CHECK(object_store_put(&store, narrow_id, type_id, make_id(3, 1),
                           make_id(3, 1), 2, narrow_payload,
                           sizeof(narrow_payload)));

    wide_sector = first_sector_of(&store, wide_id);
    narrow_sector = first_sector_of(&store, narrow_id);
    CHECK(wide_sector >= 3);
    CHECK(narrow_sector >= 3);
    CHECK(wide_sector != narrow_sector);

    wide_tail = (OBJECT_HEADER_SIZE + sizeof(wide_payload)) -
                OBJECT_STORE_SECTOR_SIZE;
    CHECK(region_is_zero(disk.bytes[wide_sector + 1], wide_tail,
                         OBJECT_STORE_SECTOR_SIZE));

    narrow_tail = OBJECT_HEADER_SIZE + sizeof(narrow_payload);
    CHECK(region_is_zero(disk.bytes[narrow_sector], narrow_tail,
                         OBJECT_STORE_SECTOR_SIZE));

    for (index = narrow_tail; index < OBJECT_STORE_SECTOR_SIZE; index++) {
        CHECK(disk.bytes[narrow_sector][index] != 0xAA);
    }
}

static void test_cache_rejects_object_larger_than_arena(void) {
    static struct pressure_disk disk;
    struct object_store store;
    struct object_table_entry entries[PRESSURE_TABLE];
    struct object_table_entry scratch[PRESSURE_TABLE];
    _Alignas(16) uint8_t cache[PRESSURE_CACHE_BYTES];
    uint8_t bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t arena[OBJECT_STORE_SECTOR_SIZE];
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id small_id;
    struct object_id big_id;
    struct object_id type_id;
    uint8_t small_payload[3];
    uint8_t big_payload[900];
    size_t used_after_small;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    small_id = make_id(70, 1);
    big_id = make_id(70, 2);
    type_id = make_id(2, 1);
    fill_payload(small_payload, sizeof(small_payload), 5);
    fill_payload(big_payload, sizeof(big_payload), 9);

    CHECK(object_store_format(&store, make_pressure_io(&disk), entries,
                              scratch, PRESSURE_TABLE, cache, sizeof(cache),
                              bitmap, sizeof(bitmap), arena, sizeof(arena)));

    CHECK(object_store_put(&store, small_id, type_id, make_id(3, 1),
                           make_id(3, 1), 1, small_payload,
                           sizeof(small_payload)));
    CHECK(cache_holds(&store, small_id));
    CHECK(store.cache_slot_count == 1);
    used_after_small = store.arena_used;
    CHECK(used_after_small > 0);

    CHECK(object_store_put(&store, big_id, type_id, make_id(3, 1),
                           make_id(3, 1), 2, big_payload,
                           sizeof(big_payload)));
    CHECK(!cache_holds(&store, big_id));
    CHECK(cache_holds(&store, small_id));
    CHECK(store.cache_slot_count == 1);
    CHECK(store.arena_used == used_after_small);
    CHECK(arena_is_consistent(&store));

    CHECK(object_store_get(&store, big_id, &header, &payload, &payload_size));
    CHECK(payload_size == sizeof(big_payload));
    CHECK(bytes_equal(payload, big_payload, sizeof(big_payload)));
    CHECK(!cache_holds(&store, big_id));
    CHECK(store.cache_slot_count == 1);
    CHECK(store.arena_used == used_after_small);
    CHECK(arena_is_consistent(&store));

    CHECK(object_store_get(&store, small_id, &header, &payload,
                           &payload_size));
    CHECK(payload_size == sizeof(small_payload));
    CHECK(bytes_equal(payload, small_payload, sizeof(small_payload)));
}

static void test_cache_evicts_least_recently_used(void) {
    static struct pressure_disk disk;
    struct object_store store;
    struct object_store cold;
    struct object_table_entry entries[PRESSURE_TABLE];
    struct object_table_entry scratch[PRESSURE_TABLE];
    struct object_table_entry cold_entries[PRESSURE_TABLE];
    struct object_table_entry cold_scratch[PRESSURE_TABLE];
    _Alignas(16) uint8_t cache[PRESSURE_CACHE_BYTES];
    uint8_t bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t arena[PRESSURE_CACHE_BYTES];
    _Alignas(16) uint8_t cold_cache[PRESSURE_CACHE_BYTES];
    uint8_t cold_bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t cold_arena[3u * OBJECT_STORE_SECTOR_SIZE];
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id ids[4];
    struct object_id type_id;
    uint8_t payloads[4][16];
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_pressure_io(&disk), entries,
                              scratch, PRESSURE_TABLE, cache, sizeof(cache),
                              bitmap, sizeof(bitmap), arena, sizeof(arena)));

    for (index = 0; index < 4; index++) {
        ids[index] = make_id(80, index + 1);
        fill_payload(payloads[index], sizeof(payloads[index]),
                     (uint8_t)(index + 1));
        CHECK(object_store_put(&store, ids[index], type_id, make_id(3, 1),
                               make_id(3, 1), index + 1, payloads[index],
                               sizeof(payloads[index])));
    }

    CHECK(object_store_mount(&cold, make_pressure_io(&disk), cold_entries,
                             cold_scratch, PRESSURE_TABLE, cold_cache,
                             sizeof(cold_cache), cold_bitmap,
                             sizeof(cold_bitmap), cold_arena,
                             sizeof(cold_arena)));
    CHECK(cold.arena_used == 0);
    CHECK(cold.cache_slot_count == 0);

    for (index = 0; index < 3; index++) {
        CHECK(object_store_get(&cold, ids[index], &header, &payload,
                               &payload_size));
        CHECK(bytes_equal(payload, payloads[index], sizeof(payloads[index])));
    }
    CHECK(cold.cache_slot_count == 3);
    CHECK(cold.arena_used == sizeof(cold_arena));
    CHECK(arena_is_consistent(&cold));

    CHECK(object_store_get(&cold, ids[0], &header, &payload, &payload_size));
    CHECK(bytes_equal(payload, payloads[0], sizeof(payloads[0])));

    CHECK(object_store_get(&cold, ids[3], &header, &payload, &payload_size));
    CHECK(bytes_equal(payload, payloads[3], sizeof(payloads[3])));

    CHECK(cold.cache_slot_count == 3);
    CHECK(cold.arena_used == sizeof(cold_arena));
    CHECK(arena_is_consistent(&cold));
    CHECK(cache_holds(&cold, ids[0]));
    CHECK(!cache_holds(&cold, ids[1]));
    CHECK(cache_holds(&cold, ids[2]));
    CHECK(cache_holds(&cold, ids[3]));

    CHECK(object_store_get(&cold, ids[1], &header, &payload, &payload_size));
    CHECK(payload_size == sizeof(payloads[1]));
    CHECK(bytes_equal(payload, payloads[1], sizeof(payloads[1])));
    CHECK(arena_is_consistent(&cold));
}

static void test_cache_compaction_survives_eviction_cycles(void) {
    static struct pressure_disk disk;
    struct object_store store;
    struct object_store cold;
    struct object_table_entry entries[PRESSURE_TABLE];
    struct object_table_entry scratch[PRESSURE_TABLE];
    struct object_table_entry cold_entries[PRESSURE_TABLE];
    struct object_table_entry cold_scratch[PRESSURE_TABLE];
    _Alignas(16) uint8_t cache[PRESSURE_CACHE_BYTES];
    uint8_t bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t arena[PRESSURE_CACHE_BYTES];
    _Alignas(16) uint8_t cold_cache[PRESSURE_CACHE_BYTES];
    uint8_t cold_bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t cold_arena[3u * OBJECT_STORE_SECTOR_SIZE];
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id ids[6];
    struct object_id type_id;
    uint8_t payloads[6][24];
    size_t round;
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_pressure_io(&disk), entries,
                              scratch, PRESSURE_TABLE, cache, sizeof(cache),
                              bitmap, sizeof(bitmap), arena, sizeof(arena)));

    for (index = 0; index < 6; index++) {
        ids[index] = make_id(90, index + 1);
        fill_payload(payloads[index], sizeof(payloads[index]),
                     (uint8_t)(index + 40));
        CHECK(object_store_put(&store, ids[index], type_id, make_id(3, 1),
                               make_id(3, 1), index + 1, payloads[index],
                               sizeof(payloads[index])));
    }

    CHECK(object_store_mount(&cold, make_pressure_io(&disk), cold_entries,
                             cold_scratch, PRESSURE_TABLE, cold_cache,
                             sizeof(cold_cache), cold_bitmap,
                             sizeof(cold_bitmap), cold_arena,
                             sizeof(cold_arena)));

    for (round = 0; round < 5; round++) {
        for (index = 0; index < 6; index++) {
            size_t pick = (index * 5u + round) % 6u;

            CHECK(object_store_get(&cold, ids[pick], &header, &payload,
                                   &payload_size));
            CHECK(payload_size == sizeof(payloads[pick]));
            CHECK(bytes_equal(payload, payloads[pick],
                              sizeof(payloads[pick])));
            CHECK(arena_is_consistent(&cold));
            CHECK(cold.cache_slot_count <= 3);
        }
    }

    for (index = 0; index < 6; index++) {
        CHECK(object_store_get(&cold, ids[index], &header, &payload,
                               &payload_size));
        CHECK(bytes_equal(payload, payloads[index], sizeof(payloads[index])));
    }
}

static void test_cache_evicts_when_slots_run_out(void) {
    static struct pressure_disk disk;
    struct object_store store;
    struct object_store cold;
    struct object_table_entry entries[PRESSURE_TABLE];
    struct object_table_entry scratch[PRESSURE_TABLE];
    struct object_table_entry cold_entries[PRESSURE_TABLE];
    struct object_table_entry cold_scratch[PRESSURE_TABLE];
    _Alignas(16) uint8_t cache[PRESSURE_CACHE_BYTES];
    uint8_t bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t arena[PRESSURE_CACHE_BYTES];
    _Alignas(16) uint8_t cold_cache[PRESSURE_CACHE_BYTES];
    uint8_t cold_bitmap[PRESSURE_BITMAP_BYTES];
    _Alignas(16) uint8_t cold_arena[PRESSURE_ROOMY_ARENA];
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id ids[PRESSURE_OBJECTS];
    struct object_id type_id;
    uint8_t payloads[PRESSURE_OBJECTS][8];
    size_t index;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    type_id = make_id(2, 1);

    CHECK(object_store_format(&store, make_pressure_io(&disk), entries,
                              scratch, PRESSURE_TABLE, cache, sizeof(cache),
                              bitmap, sizeof(bitmap), arena, sizeof(arena)));

    for (index = 0; index < PRESSURE_OBJECTS; index++) {
        ids[index] = make_id(100, index + 1);
        fill_payload(payloads[index], sizeof(payloads[index]),
                     (uint8_t)(index + 3));
        CHECK(object_store_put(&store, ids[index], type_id, make_id(3, 1),
                               make_id(3, 1), index + 1, payloads[index],
                               sizeof(payloads[index])));
    }

    CHECK(object_store_mount(&cold, make_pressure_io(&disk), cold_entries,
                             cold_scratch, PRESSURE_TABLE, cold_cache,
                             sizeof(cold_cache), cold_bitmap,
                             sizeof(cold_bitmap), cold_arena,
                             sizeof(cold_arena)));

    for (index = 0; index < PRESSURE_OBJECTS; index++) {
        CHECK(object_store_get(&cold, ids[index], &header, &payload,
                               &payload_size));
        CHECK(bytes_equal(payload, payloads[index], sizeof(payloads[index])));
        CHECK(arena_is_consistent(&cold));
    }

    CHECK(cold.cache_slot_count == OBJECT_STORE_CACHE_SLOTS);
    CHECK(cold.arena_used <
          (PRESSURE_OBJECTS * (size_t)OBJECT_STORE_SECTOR_SIZE));

    for (index = 0; index < PRESSURE_OBJECTS; index++) {
        int expected = index >= (PRESSURE_OBJECTS - OBJECT_STORE_CACHE_SLOTS);

        CHECK(cache_holds(&cold, ids[index]) == expected);
    }
}

int main(void) {
    test_commit_and_remount();
    test_crash_after_wal_recovers();
    test_snapshot_create_and_rollback();
    test_rollback_crash_after_wal_recovers();
    test_get_hot_cold_and_after_rollback();
    test_snapshot_discard();
    test_collect_reclaims_freed_sectors();
    test_prune_keeps_newest();
    test_written_sectors_are_zero_padded();
    test_cache_rejects_object_larger_than_arena();
    test_cache_evicts_least_recently_used();
    test_cache_compaction_survives_eviction_cycles();
    test_cache_evicts_when_slots_run_out();
    printf("test_object_store: %lu checks passed\n", checks_passed);
    return 0;
}
