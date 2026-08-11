#include <stddef.h>

#include "cap_table.h"
#include "../lib/string.h"

void capability_table_init(struct capability_table *table)
{
    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));

    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        table->parents[slot] = CAP_SLOT_NONE;
    }
}

int capability_table_insert_root(
    struct capability_table *table,
    const struct capability *capability,
    uint32_t *slot_out)
{
    if (table == NULL || !capability_is_valid(capability)) {
        return 0;
    }

    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        if (object_id_is_zero(table->slots[slot].object)) {
            table->slots[slot] = *capability;
            table->parents[slot] = CAP_SLOT_NONE;

            if (slot_out != NULL) {
                *slot_out = slot;
            }

            return 1;
        }
    }

    return 0;
}

const struct capability *capability_table_get(
    const struct capability_table *table,
    uint32_t slot)
{
    if (table == NULL || slot >= CAP_TABLE_SLOTS) {
        return NULL;
    }

    if (!capability_is_valid(&table->slots[slot])) {
        return NULL;
    }

    return &table->slots[slot];
}

int capability_table_derive(
    struct capability_table *table,
    uint32_t parent_slot,
    uint32_t child_rights,
    uint32_t child_badge,
    uint32_t *child_slot_out)
{
    const struct capability *parent;
    struct capability child;

    parent = capability_table_get(table, parent_slot);
    if (!capability_derive(parent, child_rights, child_badge, &child)) {
        return 0;
    }

    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        if (object_id_is_zero(table->slots[slot].object)) {
            table->slots[slot] = child;
            table->parents[slot] = parent_slot;

            if (child_slot_out != NULL) {
                *child_slot_out = slot;
            }

            return 1;
        }
    }

    return 0;
}

int capability_table_revoke(struct capability_table *table, uint32_t slot)
{
    uint8_t revoked[CAP_TABLE_SLOTS] = {0};
    int changed;

    if (capability_table_get(table, slot) == NULL) {
        return 0;
    }

    revoked[slot] = 1;

    do {
        changed = 0;

        for (uint32_t child = 0; child < CAP_TABLE_SLOTS; child++) {
            uint32_t parent = table->parents[child];

            if (!revoked[child] && parent < CAP_TABLE_SLOTS &&
                revoked[parent]) {
                revoked[child] = 1;
                changed = 1;
            }
        }
    } while (changed);

    for (uint32_t current = 0; current < CAP_TABLE_SLOTS; current++) {
        if (revoked[current]) {
            memset(&table->slots[current], 0, sizeof(table->slots[current]));
            table->parents[current] = CAP_SLOT_NONE;
        }
    }

    return 1;
}
int capability_table_find(
    const struct capability_table *table,
    struct object_id object,
    uint32_t *slot_out)
{
    if (table == NULL || slot_out == NULL || object_id_is_zero(object)) {
        return 0;
    }

    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        const struct capability *capability;

        capability = capability_table_get(table, slot);

        if (capability != NULL &&
            object_id_equal(capability->object, object)) {
            *slot_out = slot;
            return 1;
        }
    }

    return 0;
}

int capability_table_is_valid(const struct capability_table *table)
{
    if (table == NULL) {
        return 0;
    }

    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        uint32_t current;
        uint32_t depth;

        if (object_id_is_zero(table->slots[slot].object)) {
            if (table->slots[slot].rights != 0 ||
                table->slots[slot].badge != 0 ||
                table->parents[slot] != CAP_SLOT_NONE) {
                return 0;
            }

            continue;
        }

        if (!capability_is_valid(&table->slots[slot])) {
            return 0;
        }

        current = slot;

        for (depth = 0; depth < CAP_TABLE_SLOTS; depth++) {
            uint32_t parent_slot;
            const struct capability *child;
            const struct capability *parent;

            parent_slot = table->parents[current];

            if (parent_slot == CAP_SLOT_NONE) {
                break;
            }

            if (parent_slot >= CAP_TABLE_SLOTS) {
                return 0;
            }

            child = capability_table_get(table, current);
            parent = capability_table_get(table, parent_slot);

            if (!capability_is_valid(parent) ||
                !object_id_equal(child->object, parent->object) ||
                !capability_allows(parent, CAP_RIGHT_GRANT) ||
                (child->rights & parent->rights) != child->rights) {
                return 0;
            }

            current = parent_slot;
        }

        if (depth == CAP_TABLE_SLOTS) {
            return 0;
        }
    }

    return 1;
}
