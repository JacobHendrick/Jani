#include "keys.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"

int net_identity_open(struct object_store *store, net_entropy_fn entropy,
                       void *context, struct net_identity *out) {
    if (store == NULL || out == NULL) return 0;
    if (object_store_requires_recovery(store)) return -1;
    struct object_id id = {NET_KEYS_HIGH, NET_KEYS_LOW};
    uint8_t seed[32] = {0}, record[40] = {0};
    struct net_identity identity;
    int result = 0;
    if (object_table_find(&store->table, id) != NULL) {
        struct object_header header; const uint8_t *payload; size_t length;
        if (object_store_get(store, id, &header, &payload, &length) &&
            header.type_id.high == 0 && header.type_id.low == NET_KEYS_TYPE &&
            net_seed_decode(payload, length, seed) &&
            net_identity_from_seed(&identity, seed)) result = 1;
    } else if (entropy != NULL && entropy(context, seed, sizeof(seed)) == 1 &&
               net_identity_from_seed(&identity, seed)) {
        memcpy(record, "JK5S", 4); record[4] = 1; memcpy(record + 8, seed, 32);
        struct object_store_put_request request = {
            .id = id, .type_id = {0, NET_KEYS_TYPE},
            .payload = record, .payload_size = sizeof(record)};
        enum object_store_batch_result commit = object_store_put_many(store, &request, 1);
        result = commit == OBJECT_STORE_BATCH_COMMITTED ? 1 :
            commit == OBJECT_STORE_BATCH_RECOVERY_REQUIRED ? -1 : 0;
    }
    if (result == 1) *out = identity;
    crypto_wipe(&identity, sizeof(identity));
    crypto_wipe(seed, sizeof(seed)); crypto_wipe(record, sizeof(record));
    return result;
}

int net_session_random(struct net_session *session, const struct net_identity *identity,
                        const uint8_t peer[32], net_entropy_fn entropy, void *context) {
    uint8_t secret[32] = {0};
    if (entropy == NULL) return 0;
    int result = entropy(context, secret, sizeof(secret)) == 1 &&
        net_session_init(session, identity, peer, secret);
    crypto_wipe(secret, sizeof(secret));
    return result;
}
