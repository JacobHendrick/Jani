#include <stddef.h>

#include "object_table.h"

void object_table_init(
    struct object_table *table,
    struct object_table_entry *storage,
    size_t capacity
) {
    if (table == NULL) {
        return;
    }

    table->entries = storage;
    table->count = 0;

    if (storage == NULL) {
        table->capacity = 0;
    } else {
        table->capacity = capacity;
    }
}

const struct object_table_entry *object_table_find(
    const struct object_table *table,
    struct object_id id
) {
    size_t left;
    size_t right;

    if ((table == NULL) ||
        (table->entries == NULL) ||
        (table->count > table->capacity) ||
        object_id_is_zero(id)) {
        return NULL;
    }

    left = 0;
    right = table->count;

    while (left < right) {
        size_t middle;
        int comparison;

        middle = left + ((right - left) / 2);
        comparison =
            object_id_compare(table->entries[middle].id, id);

        if (comparison < 0) {
            left = middle + 1;
        } else if (comparison > 0) {
            right = middle;
        } else {
            return &table->entries[middle];
        }
    }

    return NULL;
}

int object_table_upsert(
    struct object_table *table,
    struct object_table_entry entry
) {
    size_t left;
    size_t right;
    size_t position;

    if ((table == NULL) ||
        (table->entries == NULL) ||
        (table->count > table->capacity) ||
        object_id_is_zero(entry.id) ||
        (entry.version == 0) ||
        (entry.first_sector == 0) ||
        (entry.sector_count == 0)) {
        return 0;
    }

    left = 0;
    right = table->count;

    while (left < right) {
        size_t middle;
        int comparison;

        middle = left + ((right - left) / 2);
        comparison =
            object_id_compare(table->entries[middle].id, entry.id);

        if (comparison < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    position = left;

    if ((position < table->count) &&
        object_id_equal(table->entries[position].id, entry.id)) {
        if (entry.version <= table->entries[position].version) {
            return 0;
        }

        table->entries[position] = entry;
        return 1;
    }

    if (table->count == table->capacity) {
        return 0;
    }

    {
        size_t index = table->count;

        while (index > position) {
            table->entries[index] = table->entries[index - 1];
            index--;
        }
    }

    table->entries[position] = entry;
    table->count++;

    return 1;
}
