#ifndef JANI_KERNEL_CAP_DERIVATION_H
#define JANI_KERNEL_CAP_DERIVATION_H

#include <stddef.h>
#include <stdint.h>

#include "cap_table.h"

#define CAP_DERIVATION_MAX 64u
#define CAPABILITY_REF_SIZE 24u
#define CAPABILITY_DERIVATION_SIZE 48u

/* A slot is globally identified by its owner, local index, and generation. */
struct capability_ref {
    struct object_id component_id;
    uint32_t slot;
    uint32_t generation;
};

_Static_assert(
    sizeof(struct capability_ref) == CAPABILITY_REF_SIZE,
    "capability reference must be exactly 24 bytes"
);

struct capability_derivation {
    struct capability_ref parent;
    struct capability_ref child;
};

_Static_assert(
    sizeof(struct capability_derivation) == CAPABILITY_DERIVATION_SIZE,
    "capability derivation must be exactly 48 bytes"
);

struct capability_derivation_table {
    struct capability_derivation records[CAP_DERIVATION_MAX];
    uint32_t count;
};

struct capability_ref capability_ref_make(
    struct object_id component_id,
    uint32_t slot,
    uint32_t generation
);

int capability_ref_is_valid(const struct capability_ref *reference);

int capability_ref_equal(
    struct capability_ref left,
    struct capability_ref right
);

void capability_derivation_table_init(
    struct capability_derivation_table *table
);

int capability_derivation_table_is_valid(
    const struct capability_derivation_table *table
);

int capability_derivation_add(
    struct capability_derivation_table *table,
    struct capability_ref parent,
    struct capability_ref child
);

int capability_derivation_parent(
    const struct capability_derivation_table *table,
    struct capability_ref child,
    struct capability_ref *parent_out
);

/*
 * Removes root's recorded descendants, but not root itself. The operation is
 * atomic: insufficient output capacity leaves the table unchanged.
 */
int capability_derivation_remove_subtree(
    struct capability_derivation_table *table,
    struct capability_ref root,
    struct capability_ref *descendants_out,
    size_t capacity,
    size_t *count_out
);

#endif
