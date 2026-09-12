#ifndef JANI_KERNEL_CAP_PROVENANCE_H
#define JANI_KERNEL_CAP_PROVENANCE_H

#include "../obj/object_store.h"

#define PROVENANCE_OBJECTS 32u
#define PROVENANCE_EVENTS 8u
#define PROVENANCE_BYTES (24u + PROVENANCE_OBJECTS * 280u)

enum provenance_operation {
    PROVENANCE_READ = 1, PROVENANCE_WRITE, PROVENANCE_SEND,
    PROVENANCE_DERIVE, PROVENANCE_REVOKE, PROVENANCE_CREATE
};

struct provenance_event {
    struct object_id component;
    uint64_t logical_time;
    uint32_t operation;
    int32_t result;
};

struct provenance_bucket {
    struct object_id object;
    uint32_t count;
    uint32_t next;
    struct provenance_event events[PROVENANCE_EVENTS];
};

struct provenance_ledger {
    struct provenance_bucket objects[PROVENANCE_OBJECTS];
};

_Static_assert(sizeof(struct provenance_bucket) == 280, "provenance bucket layout");
int provenance_append(struct provenance_ledger *ledger, struct object_id object,
                       struct object_id component, uint64_t time, uint32_t operation, int32_t result);
int provenance_query(const struct provenance_ledger *ledger, struct object_id object,
                      uint32_t age, struct provenance_event *event);
int provenance_encode(const struct provenance_ledger *ledger, uint8_t *bytes, size_t capacity);
int provenance_validate(const uint8_t *bytes, size_t length);
int provenance_load(struct object_store *store, struct provenance_ledger *ledger);
struct object_store_put_request provenance_request(const uint8_t *bytes);

#endif
