#ifndef JANI_KERNEL_NET_NODE_IDENTITY_H
#define JANI_KERNEL_NET_NODE_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#define NODE_PUBLIC_KEY_SIZE 32

struct node_public_key {
    uint8_t bytes[NODE_PUBLIC_KEY_SIZE];
};

int node_public_key_equal(struct node_public_key left,
                         struct node_public_key right);

/* Length-only decoding, not authentication. Returns 1 on success; on failure,
 * returns 0 without changing out. Non-null pointers must refer to stable,
 * readable input and writable output. Input and output may overlap. */
int node_public_key_decode(const uint8_t *bytes, size_t length,
                          struct node_public_key *out);

#endif // JANI_KERNEL_NET_NODE_IDENTITY_H
