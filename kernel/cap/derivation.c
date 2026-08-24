#include "derivation.h"

#include "../lib/string.h"

static int capability_ref_is_zero(struct capability_ref reference)
{
    return object_id_is_zero(reference.component_id) &&
           reference.slot == 0 &&
           reference.generation == 0;
}

static int derivation_is_zero(
    const struct capability_derivation *derivation)
{
    return capability_ref_is_zero(derivation->parent) &&
           capability_ref_is_zero(derivation->child);
}

struct capability_ref capability_ref_make(
    struct object_id component_id,
    uint32_t slot,
    uint32_t generation)
{
    struct capability_ref reference;

    reference.component_id = component_id;
    reference.slot = slot;
    reference.generation = generation;
    return reference;
}

int capability_ref_is_valid(const struct capability_ref *reference)
{
    if (reference == NULL) {
        return 0;
    }

    return !object_id_is_zero(reference->component_id) &&
           reference->slot < CAP_TABLE_SLOTS &&
           reference->generation != 0;
}

int capability_ref_equal(
    struct capability_ref left,
    struct capability_ref right)
{
    return object_id_equal(left.component_id, right.component_id) &&
           left.slot == right.slot &&
           left.generation == right.generation;
}

void capability_derivation_table_init(
    struct capability_derivation_table *table)
{
    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
}

static int find_child_record(
    const struct capability_derivation_table *table,
    struct capability_ref child,
    uint32_t *index_out)
{
    uint32_t index;

    for (index = 0; index < table->count; index++) {
        if (capability_ref_equal(table->records[index].child, child)) {
            if (index_out != NULL) {
                *index_out = index;
            }

            return 1;
        }
    }

    return 0;
}

int capability_derivation_table_is_valid(
    const struct capability_derivation_table *table)
{
    uint32_t index;

    if (table == NULL || table->count > CAP_DERIVATION_MAX) {
        return 0;
    }

    for (index = table->count; index < CAP_DERIVATION_MAX; index++) {
        if (!derivation_is_zero(&table->records[index])) {
            return 0;
        }
    }

    for (index = 0; index < table->count; index++) {
        const struct capability_derivation *record;
        struct capability_ref current;
        uint32_t other;
        uint32_t depth;

        record = &table->records[index];

        if (!capability_ref_is_valid(&record->parent) ||
            !capability_ref_is_valid(&record->child) ||
            capability_ref_equal(record->parent, record->child)) {
            return 0;
        }

        for (other = 0; other < index; other++) {
            if (capability_ref_equal(
                    table->records[other].child,
                    record->child)) {
                return 0;
            }
        }

        current = record->parent;

        for (depth = 0; depth < table->count; depth++) {
            uint32_t parent_index;

            if (capability_ref_equal(current, record->child)) {
                return 0;
            }

            if (!find_child_record(table, current, &parent_index)) {
                break;
            }

            current = table->records[parent_index].parent;
        }

        if (depth == table->count) {
            return 0;
        }
    }

    return 1;
}

int capability_derivation_add(
    struct capability_derivation_table *table,
    struct capability_ref parent,
    struct capability_ref child)
{
    struct capability_derivation *record;

    if (table == NULL ||
        !capability_derivation_table_is_valid(table) ||
        !capability_ref_is_valid(&parent) ||
        !capability_ref_is_valid(&child) ||
        capability_ref_equal(parent, child) ||
        table->count == CAP_DERIVATION_MAX ||
        find_child_record(table, child, NULL)) {
        return 0;
    }

    record = &table->records[table->count];
    record->parent = parent;
    record->child = child;
    table->count++;

    if (!capability_derivation_table_is_valid(table)) {
        table->count--;
        memset(record, 0, sizeof(*record));
        return 0;
    }

    return 1;
}

int capability_derivation_parent(
    const struct capability_derivation_table *table,
    struct capability_ref child,
    struct capability_ref *parent_out)
{
    uint32_t index;

    if (parent_out == NULL ||
        !capability_ref_is_valid(&child) ||
        !capability_derivation_table_is_valid(table) ||
        !find_child_record(table, child, &index)) {
        return 0;
    }

    *parent_out = table->records[index].parent;
    return 1;
}

static int ref_in_list(
    const struct capability_ref *references,
    size_t count,
    struct capability_ref target)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (capability_ref_equal(references[index], target)) {
            return 1;
        }
    }

    return 0;
}

int capability_derivation_remove_subtree(
    struct capability_derivation_table *table,
    struct capability_ref root,
    struct capability_ref *descendants_out,
    size_t capacity,
    size_t *count_out)
{
    struct capability_ref descendants[CAP_DERIVATION_MAX];
    size_t descendant_count;
    uint32_t read_index;
    uint32_t write_index;
    int changed;

    if (!capability_derivation_table_is_valid(table) ||
        !capability_ref_is_valid(&root) ||
        count_out == NULL ||
        (capacity != 0 && descendants_out == NULL)) {
        return 0;
    }

    descendant_count = 0;

    do {
        changed = 0;

        for (read_index = 0;
             read_index < table->count;
             read_index++) {
            const struct capability_derivation *record;

            record = &table->records[read_index];

            if (ref_in_list(
                    descendants,
                    descendant_count,
                    record->child)) {
                continue;
            }

            if (capability_ref_equal(record->parent, root) ||
                ref_in_list(
                    descendants,
                    descendant_count,
                    record->parent)) {
                descendants[descendant_count] = record->child;
                descendant_count++;
                changed = 1;
            }
        }
    } while (changed);

    if (descendant_count > capacity) {
        return 0;
    }

    for (size_t index = 0; index < descendant_count; index++) {
        descendants_out[index] = descendants[index];
    }

    write_index = 0;

    for (read_index = 0; read_index < table->count; read_index++) {
        if (ref_in_list(
                descendants,
                descendant_count,
                table->records[read_index].child)) {
            continue;
        }

        if (write_index != read_index) {
            table->records[write_index] = table->records[read_index];
        }

        write_index++;
    }

    memset(
        &table->records[write_index],
        0,
        (table->count - write_index) * sizeof(table->records[0])
    );

    table->count = write_index;
    *count_out = descendant_count;
    return 1;
}
