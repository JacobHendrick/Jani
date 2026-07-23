#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_store.h"
#include "check.h"

#define DISK_SECTORS 128u
#define TABLE_CAPACITY 8u
#define CACHE_BYTES 4096u

unsigned long checks_passed;

struct hosted_disk {
    uint8_t bytes[DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
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
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));
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
                             remounted_cache, sizeof(remounted_cache)));
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
    _Alignas(16) uint8_t recovered_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));
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
                             recovered_cache, sizeof(recovered_cache)));
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
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));

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
                             remounted_cache, sizeof(remounted_cache)));
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
    _Alignas(16) uint8_t recovered_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));
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
                             recovered_cache, sizeof(recovered_cache)));
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
    _Alignas(16) uint8_t cold_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));
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
    CHECK(store.cache_valid == 0);
    CHECK(object_store_get(&store, id, &header, &payload, &payload_size));
    CHECK(header.version == 1);
    CHECK(payload_size == sizeof(first_payload));
    CHECK(bytes_equal(payload, first_payload, sizeof(first_payload)));

    CHECK(object_store_mount(&cold, make_io(&disk), cold_entries,
                             cold_scratch, TABLE_CAPACITY, cold_cache,
                             sizeof(cold_cache)));
    CHECK(cold.cache_valid == 0);
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
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
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
                              TABLE_CAPACITY, cache, sizeof(cache)));
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
                             remounted_cache, sizeof(remounted_cache)));
    CHECK(remounted.snapshots[5].id == 0);
    CHECK(remounted.snapshots[3].id == replacement);
    CHECK(remounted.snapshots[0].id == snapshots[0]);
}

int main(void) {
    test_commit_and_remount();
    test_crash_after_wal_recovers();
    test_snapshot_create_and_rollback();
    test_rollback_crash_after_wal_recovers();
    test_get_hot_cold_and_after_rollback();
    test_snapshot_discard();
    printf("test_object_store: %lu checks passed\n", checks_passed);
    return 0;
}
