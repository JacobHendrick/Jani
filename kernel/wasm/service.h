#ifndef JANI_KERNEL_WASM_SERVICE_H
#define JANI_KERNEL_WASM_SERVICE_H

#include "component.h"
#define SERVICE_BINDING_BYTES 112u
struct service_ping { uint32_t kind; uint32_t reserved; };
struct service_counter_state { uint64_t count; int32_t slot; uint32_t reserved; };
struct service_binding {
    uint64_t magic;
    uint32_t format;
    uint32_t reserved;
    struct object_id owner;
    struct object_id module;
    struct object_id previous_module;
    uint64_t state_schema;
    uint64_t message_mask;
    uint64_t generation;
    uint64_t previous_messages;
    uint32_t state_offset;
    uint32_t state_size;
    uint32_t crc;
    uint32_t padding;
};
_Static_assert(sizeof(struct service_binding) == SERVICE_BINDING_BYTES, "service binding layout");

int service_binding_validate(const uint8_t *bytes, size_t length);
int service_bind(struct component *owner, uint64_t state_schema, uint64_t message_mask,
                  uint32_t state_offset, uint32_t state_size);
int service_swap(struct component *owner, const uint8_t *module, size_t length,
                  uint64_t state_schema, uint64_t message_mask);
int service_rollback(struct component *owner);

#endif
