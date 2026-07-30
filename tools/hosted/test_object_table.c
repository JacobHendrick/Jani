#include <stdint.h>
#include <stdio.h>

#include "../../kernel/obj/object_table.h"
#include "check.h"

unsigned long checks_passed;

static struct object_table_entry make_entry(
    uint64_t high,
    uint64_t low,
    uint64_t version,
    uint64_t first_sector,
    uint64_t sector_count
) {
    struct object_table_entry entry;

    entry.id.high = high;
    entry.id.low = low;
    entry.version = version;
    entry.first_sector = first_sector;
    entry.sector_count = sector_count;

    return entry;
}

static void test_insert_sort_and_find(void) {
    struct object_table_entry storage[3];
    struct object_table table;
    struct object_table_entry first;
    struct object_table_entry second;
    struct object_table_entry third;
    const struct object_table_entry *found;

    first = make_entry(0, 30, 1, 30, 1);
    second = make_entry(0, 10, 1, 10, 1);
    third = make_entry(0, 20, 1, 20, 1);

    object_table_init(&table, storage, 3);
    CHECK(table.count == 0);
    CHECK(table.capacity == 3);
    CHECK(table.entries == storage);
    CHECK(object_table_find(&table, first.id) == NULL);

    CHECK(object_table_upsert(&table, first));
    CHECK(object_table_upsert(&table, second));
    CHECK(object_table_upsert(&table, third));
    CHECK(table.count == 3);
    CHECK(object_id_equal(table.entries[0].id, second.id));
    CHECK(object_id_equal(table.entries[1].id, third.id));
    CHECK(object_id_equal(table.entries[2].id, first.id));

    found = object_table_find(&table, third.id);
    CHECK(found != NULL);
    CHECK(found->first_sector == 20);
}

static void test_newer_version_replaces_old_version(void) {
    struct object_table_entry storage[2];
    struct object_table table;
    struct object_table_entry original;
    struct object_table_entry newer;
    const struct object_table_entry *found;

    original = make_entry(1, 2, 1, 10, 1);
    newer = make_entry(1, 2, 2, 20, 2);

    object_table_init(&table, storage, 2);
    CHECK(object_table_upsert(&table, original));
    CHECK(!object_table_upsert(&table, original));
    CHECK(object_table_upsert(&table, newer));
    CHECK(table.count == 1);

    found = object_table_find(&table, original.id);
    CHECK(found != NULL);
    CHECK(found->version == 2);
    CHECK(found->first_sector == 20);
    CHECK(found->sector_count == 2);
}

static void test_invalid_entries_and_full_table(void) {
    struct object_table_entry storage[1];
    struct object_table table;
    struct object_table_entry valid;
    struct object_table_entry invalid;
    struct object_table_entry other;

    valid = make_entry(2, 1, 1, 1, 1);
    invalid = make_entry(0, 0, 1, 1, 1);
    other = make_entry(2, 2, 1, 2, 1);

    object_table_init(&table, storage, 1);
    CHECK(!object_table_upsert(&table, invalid));

    invalid = make_entry(2, 3, 0, 1, 1);
    CHECK(!object_table_upsert(&table, invalid));

    invalid = make_entry(2, 3, 1, 0, 1);
    CHECK(!object_table_upsert(&table, invalid));

    invalid = make_entry(2, 3, 1, 1, 0);
    CHECK(!object_table_upsert(&table, invalid));

    CHECK(object_table_upsert(&table, valid));
    CHECK(!object_table_upsert(&table, other));
    CHECK(table.count == 1);
}

int main(void) {
    test_insert_sort_and_find();
    test_newer_version_replaces_old_version();
    test_invalid_entries_and_full_table();

    printf("test_object_table: %lu checks passed\n", checks_passed);
    return 0;
}
