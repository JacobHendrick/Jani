#include "provenance.h"
#include "../lib/string.h"

int provenance_append(struct provenance_ledger *ledger, struct object_id object,
                       struct object_id component, uint64_t time, uint32_t operation, int32_t result) {
    struct provenance_bucket *bucket = NULL;
    if (ledger == NULL || object_id_is_zero(object) || object_id_is_zero(component) ||
        operation < PROVENANCE_READ || operation > PROVENANCE_CREATE) return 0;
    for (uint32_t i = 0; i < PROVENANCE_OBJECTS; i++) {
        if (object_id_equal(ledger->objects[i].object, object)) {
            bucket = &ledger->objects[i];
            break;
        }
        if (bucket == NULL && object_id_is_zero(ledger->objects[i].object)) bucket = &ledger->objects[i];
    }
    if (bucket == NULL || bucket->count > PROVENANCE_EVENTS || bucket->next >= PROVENANCE_EVENTS) return 0;
    bucket->object = object;
    bucket->events[bucket->next] = (struct provenance_event){component, time, operation, result};
    bucket->next = (bucket->next + 1) % PROVENANCE_EVENTS;
    if (bucket->count < PROVENANCE_EVENTS) bucket->count++;
    return 1;
}

int provenance_query(const struct provenance_ledger *ledger, struct object_id object,
                      uint32_t age, struct provenance_event *event) {
    if (ledger == NULL || event == NULL || age >= PROVENANCE_EVENTS) return 0;
    for (uint32_t i = 0; i < PROVENANCE_OBJECTS; i++) {
        const struct provenance_bucket *b = &ledger->objects[i];
        if (!object_id_equal(b->object, object)) continue;
        if (b->count > PROVENANCE_EVENTS || age >= b->count || b->next >= PROVENANCE_EVENTS) return 0;
        *event = b->events[(b->next + PROVENANCE_EVENTS - age - 1) % PROVENANCE_EVENTS];
        return 1;
    }
    return 0;
}

int provenance_encode(const struct provenance_ledger *ledger, uint8_t *bytes, size_t capacity) {
    const uint64_t magic = UINT64_C(0x4A414E4950525631);
    uint32_t version = 1, crc;
    if (ledger == NULL || bytes == NULL || capacity < PROVENANCE_BYTES) return 0;
    memset(bytes, 0, 24);
    memcpy(bytes, &magic, 8);
    memcpy(bytes + 8, &version, 4);
    memcpy(bytes + 24, ledger, sizeof(*ledger));
    crc = object_crc32c(bytes + 24, sizeof(*ledger));
    memcpy(bytes + 16, &crc, 4);
    return provenance_validate(bytes, PROVENANCE_BYTES);
}

int provenance_load(struct object_store *store, struct provenance_ledger *ledger) {
    struct object_header header;
    const uint8_t *bytes;
    size_t size;
    if (ledger == NULL || !object_store_get(store, (struct object_id){5, 0}, &header, &bytes, &size) ||
        !object_id_equal(header.type_id, (struct object_id){0, 8}) || !provenance_validate(bytes, size)) return 0;
    memcpy(ledger, bytes + 24, sizeof(*ledger));
    return 1;
}

struct object_store_put_request provenance_request(const uint8_t *bytes) {
    struct object_store_put_request request = {0};
    request.id = (struct object_id){5, 0};
    request.type_id = (struct object_id){0, 8};
    request.payload = bytes;
    request.payload_size = PROVENANCE_BYTES;
    return request;
}
