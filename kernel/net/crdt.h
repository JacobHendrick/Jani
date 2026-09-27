#ifndef JANI_KERNEL_NET_CRDT_H
#define JANI_KERNEL_NET_CRDT_H

#include <stddef.h>
#include <stdint.h>

#define NET_CRDT_LWW 1u
#define NET_CRDT_ORSET 2u
#define NET_CRDT_NODES 2u
#define NET_CRDT_VALUE_MAX 256u
#define NET_CRDT_DOTS_MAX 16u
#define NET_CRDT_BYTES 680u

struct net_dot {
    uint32_t node;
    uint32_t removed;
    uint64_t counter;
    uint64_t value;
};

struct net_crdt {
    uint32_t kind;
    uint32_t count;
    uint64_t vector[NET_CRDT_NODES];
    uint64_t timestamp;
    uint32_t author;
    uint32_t length;
    uint8_t value[NET_CRDT_VALUE_MAX];
    struct net_dot dots[NET_CRDT_DOTS_MAX];
};

_Static_assert(sizeof(struct net_dot) == 24, "dot ABI");
_Static_assert(sizeof(struct net_crdt) == NET_CRDT_BYTES, "CRDT ABI");

/* Node indexes are the canonical public-key order of the provisioned pair.
 * All rejected operations leave their output unchanged. Merges may alias input.
 * No tombstone GC: full history rejects further growth until stability exists. */
int net_crdt_init(struct net_crdt *out, uint32_t kind);
int net_crdt_set(struct net_crdt *state, uint32_t node,
                  const uint8_t *value, size_t length);
int net_crdt_add(struct net_crdt *state, uint32_t node, uint64_t value);
int net_crdt_remove(struct net_crdt *state, uint32_t node, uint64_t value);
int net_crdt_merge(struct net_crdt *out, const struct net_crdt *left,
                    const struct net_crdt *right);
size_t net_crdt_encode(uint8_t *out, size_t capacity, const struct net_crdt *state);
int net_crdt_decode(const uint8_t *bytes, size_t length, struct net_crdt *out);

#endif
