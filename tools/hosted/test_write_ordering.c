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
#define PAYLOAD_BYTES 64u
#define WRITES_UNLIMITED 0xFFFFFFFFUL
#define MAX_CUT_POINT 48u
#define MAX_DROPPED_FLUSH 20u
#define MAX_DROP_INDEX 4u

unsigned long checks_passed;

struct barrier_disk {
    uint8_t durable[DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    uint8_t visible[DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    uint8_t unflushed[DISK_SECTORS];
    unsigned long writes_remaining;
    int cut;
    int flush_is_noop;
    unsigned long ignore_flush_index;
    unsigned long write_count;
    unsigned long flush_count;
};

static struct barrier_disk disk;
static struct object_store_entries {
    struct object_table_entry table[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    uint8_t arena[ARENA_BYTES];
} buffers;

static int barrier_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct barrier_disk *target = context;

    if ((sector >= DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    memcpy(buffer, target->visible[sector], OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int barrier_write(
    void *context,
    uint64_t sector,
    const uint8_t *buffer
) {
    struct barrier_disk *target = context;

    if ((sector >= DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    if (target->cut) {
        return 0;
    }

    if (target->writes_remaining == 0) {
        target->cut = 1;
        return 0;
    }
    target->writes_remaining--;

    memcpy(target->visible[sector], buffer, OBJECT_STORE_SECTOR_SIZE);
    target->unflushed[sector] = 1;
    target->write_count++;

    if (target->writes_remaining == 0) {
        target->cut = 1;
    }

    return 1;
}

static int barrier_flush(void *context) {
    struct barrier_disk *target = context;
    uint64_t sector;

    if (target == NULL) {
        return 0;
    }

    if (target->cut) {
        return 0;
    }

    target->flush_count++;

    if (target->flush_is_noop ||
        (target->flush_count == target->ignore_flush_index)) {
        return 1;
    }

    for (sector = 0; sector < DISK_SECTORS; sector++) {
        if (target->unflushed[sector]) {
            memcpy(target->durable[sector], target->visible[sector],
                   OBJECT_STORE_SECTOR_SIZE);
            target->unflushed[sector] = 0;
        }
    }

    return 1;
}

static void power_cut(struct barrier_disk *target, unsigned long drop_index) {
    uint64_t sector;
    unsigned long ordinal;

    ordinal = 0;
    for (sector = 0; sector < DISK_SECTORS; sector++) {
        if (!target->unflushed[sector]) {
            continue;
        }

        ordinal++;
        if ((drop_index == 0) || (ordinal == drop_index)) {
            memcpy(target->visible[sector], target->durable[sector],
                   OBJECT_STORE_SECTOR_SIZE);
        } else {
            memcpy(target->durable[sector], target->visible[sector],
                   OBJECT_STORE_SECTOR_SIZE);
        }
        target->unflushed[sector] = 0;
    }

    target->cut = 0;
    target->writes_remaining = WRITES_UNLIMITED;
}

static struct object_store_io make_io(struct barrier_disk *target) {
    struct object_store_io io;

    io.context = target;
    io.sector_count = DISK_SECTORS;
    io.read_sector = barrier_read;
    io.write_sector = barrier_write;
    io.flush = barrier_flush;
    return io;
}

static int format_store(struct object_store *store) {
    return object_store_format(
        store, make_io(&disk), buffers.table, buffers.scratch, TABLE_CAPACITY,
        buffers.cache, CACHE_BYTES, buffers.bitmap, BITMAP_BYTES,
        buffers.arena, ARENA_BYTES
    );
}

static int mount_store(struct object_store *store) {
    return object_store_mount(
        store, make_io(&disk), buffers.table, buffers.scratch, TABLE_CAPACITY,
        buffers.cache, CACHE_BYTES, buffers.bitmap, BITMAP_BYTES,
        buffers.arena, ARENA_BYTES
    );
}

static struct object_id make_id(uint64_t high, uint64_t low) {
    struct object_id id;

    id.high = high;
    id.low = low;
    return id;
}

static void fill_payload(uint8_t *payload, uint8_t pattern) {
    size_t index;

    for (index = 0; index < PAYLOAD_BYTES; index++) {
        payload[index] = (uint8_t)(pattern + (uint8_t)index);
    }
}

static int payload_matches(const uint8_t *payload, uint8_t pattern) {
    size_t index;

    for (index = 0; index < PAYLOAD_BYTES; index++) {
        if (payload[index] != (uint8_t)(pattern + (uint8_t)index)) {
            return 0;
        }
    }

    return 1;
}

static int put_pattern(
    struct object_store *store,
    struct object_id id,
    uint8_t pattern
) {
    uint8_t payload[PAYLOAD_BYTES];

    fill_payload(payload, pattern);
    return object_store_put(store, id, make_id(0, 9), make_id(0, 1),
                            make_id(0, 1), 1, payload, PAYLOAD_BYTES);
}

static void reset_disk(int flush_is_noop, unsigned long ignore_flush_index) {
    memset(&disk, 0, sizeof(disk));
    disk.writes_remaining = WRITES_UNLIMITED;
    disk.flush_is_noop = flush_is_noop;
    disk.ignore_flush_index = ignore_flush_index;
}

static void test_model_discards_unflushed_writes(void) {
    uint8_t sector_bytes[OBJECT_STORE_SECTOR_SIZE];

    reset_disk(0, 0);

    memset(sector_bytes, 0xAB, sizeof(sector_bytes));
    CHECK(barrier_write(&disk, 5, sector_bytes) == 1);
    power_cut(&disk, 0);
    CHECK(barrier_read(&disk, 5, sector_bytes) == 1);
    CHECK(sector_bytes[0] == 0x00);

    memset(sector_bytes, 0xCD, sizeof(sector_bytes));
    CHECK(barrier_write(&disk, 5, sector_bytes) == 1);
    CHECK(barrier_flush(&disk) == 1);
    power_cut(&disk, 0);
    CHECK(barrier_read(&disk, 5, sector_bytes) == 1);
    CHECK(sector_bytes[0] == 0xCD);
}

static void test_model_noop_flush_loses_everything(void) {
    uint8_t sector_bytes[OBJECT_STORE_SECTOR_SIZE];

    reset_disk(1, 0);

    memset(sector_bytes, 0xEF, sizeof(sector_bytes));
    CHECK(barrier_write(&disk, 7, sector_bytes) == 1);
    CHECK(barrier_flush(&disk) == 1);
    power_cut(&disk, 0);
    CHECK(barrier_read(&disk, 7, sector_bytes) == 1);
    CHECK(sector_bytes[0] == 0x00);
}

static int survives_cut_at(
    unsigned long cut_point,
    int flush_is_noop,
    unsigned long ignore_flush_index,
    unsigned long drop_index
) {
    struct object_store store;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id stable_id;
    struct object_id churn_id;

    stable_id = make_id(0, 100);
    churn_id = make_id(0, 200);

    reset_disk(flush_is_noop, ignore_flush_index);

    if (!format_store(&store)) {
        return 0;
    }
    if (!put_pattern(&store, stable_id, 0x10)) {
        return 0;
    }
    if (!put_pattern(&store, churn_id, 0x20)) {
        return 0;
    }

    disk.writes_remaining = cut_point;
    (void)put_pattern(&store, churn_id, 0x30);
    power_cut(&disk, drop_index);

    if (!mount_store(&store)) {
        return 0;
    }

    if (!object_store_get(&store, stable_id, &header, &payload,
                          &payload_size)) {
        return 0;
    }
    if ((payload_size != PAYLOAD_BYTES) || !payload_matches(payload, 0x10)) {
        return 0;
    }

    if (!object_store_get(&store, churn_id, &header, &payload,
                          &payload_size)) {
        return 0;
    }
    if (payload_size != PAYLOAD_BYTES) {
        return 0;
    }
    if (!payload_matches(payload, 0x20) && !payload_matches(payload, 0x30)) {
        return 0;
    }

    return 1;
}

static void test_committed_state_survives_power_loss(void) {
    unsigned long cut_point;
    unsigned long drop_index;

    for (cut_point = 1; cut_point <= MAX_CUT_POINT; cut_point++) {
        for (drop_index = 0; drop_index <= MAX_DROP_INDEX; drop_index++) {
            CHECK(survives_cut_at(cut_point, 0, 0, drop_index) == 1);
        }
    }
}

static void test_harness_detects_no_barriers_at_all(void) {
    unsigned long cut_point;
    unsigned long detected;

    detected = 0;
    for (cut_point = 1; cut_point <= MAX_CUT_POINT; cut_point++) {
        if (!survives_cut_at(cut_point, 1, 0, 0)) {
            detected++;
        }
    }

    CHECK(detected == MAX_CUT_POINT);
    printf("write-ordering: %lu of %u cut points lose data when every flush "
           "is discarded\n", detected, MAX_CUT_POINT);
}

static void test_harness_detects_one_dropped_barrier(void) {
    unsigned long flush_index;
    unsigned long cut_point;
    unsigned long drop_index;
    unsigned long detected;

    detected = 0;
    for (flush_index = 1; flush_index <= MAX_DROPPED_FLUSH; flush_index++) {
        for (cut_point = 1; cut_point <= MAX_CUT_POINT; cut_point++) {
            for (drop_index = 1; drop_index <= MAX_DROP_INDEX; drop_index++) {
                if (!survives_cut_at(cut_point, 0, flush_index, drop_index)) {
                    detected++;
                }
            }
        }
    }

    CHECK(detected > 0);
    printf("write-ordering: %lu (flush, cut, drop) triples lose data when a "
           "single barrier is dropped\n", detected);
}

int main(int argc, char **argv) {
    checks_passed = 0;
    (void)argv;

    test_model_discards_unflushed_writes();
    test_model_noop_flush_loses_everything();

    if (argc > 1) {
        test_harness_detects_no_barriers_at_all();
        test_harness_detects_one_dropped_barrier();
        printf("test_write_ordering (negative): %lu checks passed\n",
               checks_passed);
        return 0;
    }

    test_committed_state_survives_power_loss();
    printf("test_write_ordering: %lu checks passed\n", checks_passed);
    return 0;
}
