#ifndef JANI_KERNEL_CAP_CAP_TABLE_H
#define JANI_KERNEL_CAP_CAP_TABLE_H

#include <stdint.h>

#include "capability.h"

#define CAP_TABLE_SLOTS 16u
#define CAP_SLOT_NONE UINT32_MAX

struct capability_table {
    struct capability slots[CAP_TABLE_SLOTS];
    uint32_t parents[CAP_TABLE_SLOTS];
    uint32_t generations[CAP_TABLE_SLOTS];
};

void capability_table_init(struct capability_table *table);

int capability_table_insert_root(
    struct capability_table *table,
    const struct capability *capability,
    uint32_t *slot_out
);

const struct capability *capability_table_get(
    const struct capability_table *table,
    uint32_t slot
);

uint32_t capability_table_generation(
    const struct capability_table *table,
    uint32_t slot
);

int capability_table_derive(
    struct capability_table *table,
    uint32_t parent_slot,
    uint32_t child_rights,
    uint32_t child_badge,
    uint32_t *child_slot_out
);

int capability_table_revoke(
    struct capability_table *table,
    uint32_t slot
);

int capability_table_find(
    const struct capability_table *table,
    struct object_id object,
    uint32_t *slot_out
);

int capability_table_is_valid(
    const struct capability_table *table
);

#endif
