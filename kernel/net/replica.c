#include "replica.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"

static struct object_id metadata_id(void) {
    return (struct object_id){REPLICA_META_HIGH, REPLICA_META_LOW};
}
static void put64(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8; i++) out[i] = (uint8_t)(value >> (8u*i));
}
static uint64_t namespace_for(const uint8_t key[32]) {
    uint8_t named[40], hash[32];
    memcpy(named, "JANI5-ID", 8); memcpy(named + 8, key, 32);
    crypto_blake2b(hash, 32, named, sizeof(named));
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) value |= (uint64_t)hash[i] << (8u*i);
    return value | (UINT64_C(1) << 63);
}
static int key_less(const uint8_t *left, const uint8_t *right) {
    for (size_t i = 0; i < 32; i++) if (left[i] != right[i]) return left[i] < right[i];
    return 0;
}
static int equal_bytes(const void *left, const void *right, size_t length) {
    const uint8_t *a = left, *b = right;
    for (size_t i = 0; i < length; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static int healthy(struct replica *r) {
    if (r == NULL || r->store == NULL || r->session == NULL || r->halted) return 0;
    if (object_store_requires_recovery(r->store)) { r->halted = 1; return 0; }
    return 1;
}
static int failed(const struct replica *r) { return r != NULL && r->halted ? -1 : 0; }
static struct replica_scope *find_scope(struct replica *r, struct object_id id) {
    for (uint32_t i = 0; i < r->catalog.count; i++)
        if (object_id_equal(r->catalog.scopes[i].id, id)) return &r->catalog.scopes[i];
    return NULL;
}
static int load_state(struct replica *r, const struct replica_scope *scope, struct net_crdt *out) {
    struct object_header header;
    const uint8_t *payload;
    size_t length;
    struct net_crdt state;
    if (!object_store_get(r->store, scope->id, &header, &payload, &length) ||
        header.type_id.high != 0 || header.type_id.low != REPLICA_TYPE_BASE + scope->kind ||
        !net_crdt_decode(payload, length, &state) || state.kind != scope->kind) return 0;
    *out = state;
    return 1;
}
static int publish(struct replica *r, const struct replica_catalog *catalog,
                    const struct replica_scope *scope, const struct net_crdt *state) {
    uint8_t meta[REPLICA_CATALOG_BYTES], record[NET_CRDT_BYTES];
    struct object_store_put_request requests[2];
    size_t count = 0;
    struct object_id actor = {r->local_namespace, 1};
    if (catalog != NULL) {
        if (!replica_catalog_encode(meta, sizeof(meta), r->session->identity->public_key,
                                     r->session->pinned_peer, catalog)) return 0;
        requests[count++] = (struct object_store_put_request){
            .id = metadata_id(), .type_id = {0, REPLICA_TYPE_BASE},
            .creator_id = actor, .modifier_id = actor,
            .payload = meta, .payload_size = sizeof(meta)};
    }
    if (scope != NULL && state != NULL) {
        if (!net_crdt_encode(record, sizeof(record), state)) return 0;
        requests[count++] = (struct object_store_put_request){
            .id = scope->id, .type_id = {0, REPLICA_TYPE_BASE + scope->kind},
            .creator_id = {scope->id.high, 1}, .modifier_id = actor,
            .logical_timestamp = state->timestamp,
            .payload = record, .payload_size = sizeof(record)};
    }
    enum object_store_batch_result result = object_store_put_many(r->store, requests, count);
    if (result == OBJECT_STORE_BATCH_RECOVERY_REQUIRED) { r->halted = 1; return -1; }
    if (result != OBJECT_STORE_BATCH_COMMITTED) return 0;
    if (catalog != NULL) {
        uint32_t changed = 0;
        while (changed < catalog->count && changed < r->catalog.count &&
               equal_bytes(&catalog->scopes[changed], &r->catalog.scopes[changed],
                           sizeof(struct replica_scope))) changed++;
        r->catalog = *catalog;
        if (changed < catalog->count) {
            r->cursor = changed;
            r->send_grant = 1;
        }
    }
    return 1;
}
int replica_open(struct replica *r, struct object_store *store, struct net_session *session) {
    if (r == NULL || store == NULL || session == NULL || session->identity == NULL ||
        object_store_requires_recovery(store)) return 0;
    struct replica next;
    memset(&next, 0, sizeof(next));
    next.store = store; next.session = session;
    next.send_grant = 1;
    next.local_namespace = namespace_for(session->identity->public_key);
    next.peer_namespace = namespace_for(session->pinned_peer);
    if (next.local_namespace == next.peer_namespace) return 0;
    next.local_node = key_less(session->identity->public_key, session->pinned_peer) ? 0 : 1;
    const struct object_table_entry *entry = object_table_find(&store->table, metadata_id());
    if (entry != NULL) {
        struct object_header header; const uint8_t *payload; size_t length;
        if (!object_store_get(store, metadata_id(), &header, &payload, &length) ||
            header.type_id.high != 0 || header.type_id.low != REPLICA_TYPE_BASE ||
            !replica_catalog_decode(payload, length, session->identity->public_key,
                                     session->pinned_peer, &next.catalog)) return 0;
        for (uint32_t i = 0; i < next.catalog.count; i++) {
            const struct replica_scope *s = &next.catalog.scopes[i];
            struct net_crdt state;
            if (s->id.high != (s->owned ? next.local_namespace : next.peer_namespace) ||
                !load_state(&next, s, &state)) return 0;
        }
    } else {
        int result = publish(&next, &next.catalog, NULL, NULL);
        if (result != 1) return result;
    }
    *r = next;
    return 1;
}
int replica_create(struct replica *r, uint64_t sequence, uint32_t kind, struct object_id *out) {
    if (!healthy(r)) return failed(r);
    if (sequence == 0 || out == NULL || r->catalog.count == REPLICA_OBJECTS_MAX) return 0;
    struct replica_catalog catalog = r->catalog;
    struct replica_scope scope = {.id = {r->local_namespace, sequence}, .epoch = 1,
        .kind = kind, .owned = 1};
    struct net_crdt state;
    if (object_table_find(&r->store->table, scope.id) != NULL || !net_crdt_init(&state, kind)) return 0;
    catalog.scopes[catalog.count++] = scope;
    int result = publish(r, &catalog, &scope, &state);
    if (result == 1) *out = scope.id;
    return result;
}
int replica_share(struct replica *r, struct object_id id, uint32_t rights) {
    if (!healthy(r)) return failed(r);
    struct replica_scope *scope = find_scope(r, id);
    if (scope == NULL || !scope->owned || rights > 3) return 0;
    if (scope->rights == rights) return 1;
    if (scope->epoch == UINT64_MAX) return 0;
    struct replica_catalog catalog = r->catalog;
    size_t at = (size_t)(scope - r->catalog.scopes);
    catalog.scopes[at].epoch++;
    catalog.scopes[at].rights = rights;
    return publish(r, &catalog, NULL, NULL);
}
int replica_read(struct replica *r, struct object_id id, struct net_crdt *out) {
    if (!healthy(r)) return failed(r);
    struct replica_scope *scope = find_scope(r, id);
    if (scope == NULL || out == NULL || (!scope->owned && !(scope->rights & REPLICA_READ))) return 0;
    return load_state(r, scope, out);
}
static int mutate(struct replica *r, struct object_id id, uint32_t operation,
                   const uint8_t *value, size_t length, uint64_t item) {
    if (!healthy(r)) return failed(r);
    struct replica_scope *scope = find_scope(r, id);
    struct net_crdt state;
    if (scope == NULL || (!scope->owned && !(scope->rights & REPLICA_WRITE)) ||
        !load_state(r, scope, &state)) return 0;
    int result = operation == 0 ? net_crdt_set(&state, r->local_node, value, length) :
        operation == 1 ? net_crdt_add(&state, r->local_node, item) :
                         net_crdt_remove(&state, r->local_node, item);
    if (!result) return 0;
    return publish(r, NULL, scope, &state);
}
int replica_set(struct replica *r, struct object_id id, const uint8_t *value, size_t length) {
    return mutate(r, id, 0, value, length, 0);
}
int replica_add(struct replica *r, struct object_id id, uint64_t value) {
    return mutate(r, id, 1, NULL, 0, value);
}
int replica_remove(struct replica *r, struct object_id id, uint64_t value) {
    return mutate(r, id, 2, NULL, 0, value);
}
static int can_send(const struct replica_scope *s) {
    return s->rights & (s->owned ? REPLICA_READ : REPLICA_WRITE);
}
static int can_receive(const struct replica_scope *s) {
    return s->rights & (s->owned ? REPLICA_WRITE : REPLICA_READ);
}
static void message_header(uint8_t out[32], const struct replica_scope *s, uint32_t kind) {
    memset(out, 0, 32);
    memcpy(out, "JR5O", 4);
    out[4] = 1; out[5] = (uint8_t)kind; out[6] = (uint8_t)s->kind;
    out[7] = kind == NET_MESSAGE_GRANT ? (uint8_t)s->rights : 0;
    put64(out + 8, s->id.high); put64(out + 16, s->id.low); put64(out + 24, s->epoch);
}
size_t replica_next_packet(struct replica *r, uint8_t *out, size_t capacity) {
    if (!healthy(r) || !r->session->active || r->catalog.count == 0) return 0;
    for (uint32_t tries = 0; tries < 2*r->catalog.count; tries++) {
        struct replica_scope *scope = &r->catalog.scopes[r->cursor];
        uint8_t body[80], bytes[NET_CRDT_BYTES];
        uint32_t kind = NET_MESSAGE_SUMMARY; size_t length = 0;
        if (r->send_grant && scope->owned && (scope->rights != 0 || scope->epoch > 1)) {
            kind = NET_MESSAGE_GRANT; message_header(body, scope, kind); length = 32;
        } else if (!r->send_grant && scope->rights != 0) {
            struct net_crdt state;
            if (!load_state(r, scope, &state) || !net_crdt_encode(bytes, sizeof(bytes), &state)) return 0;
            message_header(body, scope, kind);
            put64(body + 32, state.vector[0]); put64(body + 40, state.vector[1]);
            crypto_blake2b(body + 48, 32, bytes, sizeof(bytes)); length = sizeof(body);
        }
        if (length != 0) {
            size_t result = net_session_seal(r->session, kind, body, length, out, capacity);
            if (result == 0) return 0;
            r->cursor++;
            if (r->cursor == r->catalog.count) { r->cursor = 0; r->send_grant ^= 1; }
            return result;
        }
        r->cursor++;
        if (r->cursor == r->catalog.count) { r->cursor = 0; r->send_grant ^= 1; }
    }
    return 0;
}
static int accept_grant(struct replica *r, const struct replica_message *message) {
    if (message->id.high != r->peer_namespace) return 0;
    struct replica_scope *scope = find_scope(r, message->id);
    struct replica_catalog catalog = r->catalog;
    if (scope != NULL) {
        if (scope->owned || scope->kind != message->kind || message->epoch < scope->epoch) return 0;
        if (message->epoch == scope->epoch) return scope->rights == message->rights;
        size_t at = (size_t)(scope - r->catalog.scopes);
        catalog.scopes[at].epoch = message->epoch;
        catalog.scopes[at].rights = message->rights;
        return publish(r, &catalog, NULL, NULL);
    }
    if (message->rights == 0) return 1;
    if (catalog.count == REPLICA_OBJECTS_MAX ||
        object_table_find(&r->store->table, message->id) != NULL) return 0;
    struct replica_scope added = {.id = message->id, .epoch = message->epoch,
        .kind = message->kind, .rights = message->rights};
    struct net_crdt empty;
    if (!net_crdt_init(&empty, added.kind)) return 0;
    catalog.scopes[catalog.count++] = added;
    return publish(r, &catalog, &added, &empty);
}
int replica_receive(struct replica *r, const uint8_t *packet, size_t length,
                     uint8_t *reply, size_t capacity, size_t *reply_length) {
    if (!healthy(r)) return failed(r);
    if (reply_length == NULL || reply == NULL) return 0;
    uint8_t body[NET_BODY_MAX]; uint32_t kind; size_t body_length;
    struct replica_message message;
    if (!net_session_open(r->session, packet, length, body, sizeof(body), &kind, &body_length) ||
        !replica_message_decode(body, body_length, kind, &message)) return 0;
    *reply_length = 0;
    if (kind == NET_MESSAGE_GRANT) return accept_grant(r, &message);
    struct replica_scope *scope = find_scope(r, message.id);
    if (scope == NULL || scope->kind != message.kind || scope->epoch != message.epoch ||
        (kind == NET_MESSAGE_UPDATE ? !can_receive(scope) : scope->rights == 0)) return 0;
    struct net_crdt state; uint8_t local[NET_CRDT_BYTES];
    if (!load_state(r, scope, &state) || !net_crdt_encode(local, sizeof(local), &state)) return 0;
    if (kind == NET_MESSAGE_UPDATE) {
        struct net_crdt remote, merged;
        uint8_t next[NET_CRDT_BYTES];
        if (!net_crdt_decode(body + 32, body_length - 32, &remote) || remote.kind != state.kind ||
            !net_crdt_merge(&merged, &state, &remote) ||
            !net_crdt_encode(next, sizeof(next), &merged)) return 0;
        if (equal_bytes(local, next, sizeof(next))) return 1;
        return publish(r, NULL, scope, &merged);
    }
    uint8_t digest[32];
    crypto_blake2b(digest, 32, local, sizeof(local));
    if (!can_send(scope) || (message.vector[0] == state.vector[0] &&
        message.vector[1] == state.vector[1] && crypto_verify32(message.digest, digest) == 0)) return 1;
    message_header(body, scope, NET_MESSAGE_UPDATE);
    memcpy(body + 32, local, sizeof(local));
    *reply_length = net_session_seal(r->session, NET_MESSAGE_UPDATE, body, 712, reply, capacity);
    return 1;
}
