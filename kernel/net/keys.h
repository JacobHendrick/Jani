#ifndef JANI_KERNEL_NET_KEYS_H
#define JANI_KERNEL_NET_KEYS_H

#include "session.h"
#include "../obj/object_store.h"

#define NET_KEYS_HIGH UINT64_C(0x4a414e49354e4554)
#define NET_KEYS_LOW UINT64_C(1)
#define NET_KEYS_TYPE UINT64_C(0x5008)

typedef int (*net_entropy_fn)(void *context, uint8_t *out, size_t length);

int net_seed_decode(const uint8_t *bytes, size_t length, uint8_t out[32]);
/* Protected store record; not encrypted at rest. Never regenerate a corrupt or
 * uncertain existing identity. Entropy is required only for first provisioning.
 * Returns 1, 0, or -1 (remount required), with out unchanged on failure. */
int net_identity_open(struct object_store *store, net_entropy_fn entropy,
    void *context, struct net_identity *out);
int net_session_random(struct net_session *session, const struct net_identity *identity,
    const uint8_t peer[32], net_entropy_fn entropy, void *context);

#endif
