#include "node_identity.h"

int node_public_key_equal(struct node_public_key left,
                         struct node_public_key right) {
    for (unsigned int i = 0; i < NODE_PUBLIC_KEY_SIZE; i++) {
        if (left.bytes[i] != right.bytes[i]) {
            return 0;
        }
    }

    return 1;
}
