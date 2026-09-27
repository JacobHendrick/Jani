#ifndef JANI_KERNEL_NET_REPLICA_H
#define JANI_KERNEL_NET_REPLICA_H

#include "crdt.h"
#include "session.h"
#include "../obj/object_store.h"

#define REPLICA_OBJECTS_MAX 8u
#define REPLICA_CATALOG_BYTES 400u
#define REPLICA_READ 1u
#define REPLICA_WRITE 2u
#define REPLICA_META_HIGH UINT64_C(0x4a414e49354e4554)
#define REPLICA_META_LOW UINT64_C(0)
#define REPLICA_TYPE_BASE UINT64_C(0x5000)

struct replica_scope {
    struct object_id id;
    uint64_t epoch;
    uint32_t kind;
    uint32_t rights;
    uint32_t owned;
    uint32_t reserved;
};
struct replica_catalog {
    uint32_t count;
    uint32_t reserved;
    struct replica_scope scopes[REPLICA_OBJECTS_MAX];
};
struct replica_message {
    struct object_id id;
    uint64_t epoch;
    uint32_t kind;
    uint32_t rights;
    uint64_t vector[2];
    uint8_t digest[32];
};
struct replica {
    struct object_store *store;
    struct net_session *session;
    struct replica_catalog catalog;
    uint64_t local_namespace;
    uint64_t peer_namespace;
    uint32_t local_node;
    uint32_t cursor;
    uint32_t send_grant;
    uint32_t halted;
};

_Static_assert(sizeof(struct replica_scope) == 40, "replica scope ABI");
_Static_assert(sizeof(struct replica_catalog) == 328, "replica catalog ABI");
_Static_assert(sizeof(struct replica_message) == 80, "replica message ABI");

int replica_catalog_decode(const uint8_t *bytes, size_t length,
    const uint8_t local_key[32], const uint8_t peer_key[32], struct replica_catalog *out);
size_t replica_catalog_encode(uint8_t *out, size_t capacity,
    const uint8_t local_key[32], const uint8_t peer_key[32], const struct replica_catalog *catalog);
int replica_message_decode(const uint8_t *bytes, size_t length,
    uint32_t packet_kind, struct replica_message *out);

/* Trusted coordinator only. One provisioned pair per store. No guest ABI.
 * Return 1 on success, 0 on rejection, -1 when remount is required. */
int replica_open(struct replica *replica, struct object_store *store, struct net_session *session);
int replica_create(struct replica *replica, uint64_t sequence, uint32_t kind, struct object_id *out);
int replica_share(struct replica *replica, struct object_id id, uint32_t rights);
int replica_read(struct replica *replica, struct object_id id, struct net_crdt *out);
int replica_set(struct replica *replica, struct object_id id, const uint8_t *value, size_t length);
int replica_add(struct replica *replica, struct object_id id, uint64_t value);
int replica_remove(struct replica *replica, struct object_id id, uint64_t value);
/* Call regularly. Both peers exchange summaries; replies carry missing state.
 * Transport owns retry timing; it must retain a produced packet while busy. */
size_t replica_next_packet(struct replica *replica, uint8_t *out, size_t capacity);
int replica_receive(struct replica *replica, const uint8_t *packet, size_t length,
    uint8_t *reply, size_t capacity, size_t *reply_length);

#endif
