#include "component.h"

#include "../lib/string.h"
#include "instance_state.h"

struct object_id component_make_id(uint64_t high, uint64_t low) {
    struct object_id id;

    id.high = high;
    id.low = low;
    return id;
}

int component_capability_find(
    const struct component *component,
    struct object_id object,
    uint32_t *slot_out
) {
    uint32_t index;

    if ((component == NULL) || (slot_out == NULL)) {
        return 0;
    }

    for (index = 0; index < component->capability_count; index++) {
        if (object_id_equal(component->capabilities[index].object, object)) {
            *slot_out = index;
            return 1;
        }
    }

    return 0;
}

int component_capability_insert(
    struct component *component,
    struct object_id object,
    uint32_t rights,
    uint32_t badge,
    uint32_t *slot_out
) {
    uint32_t slot;

    if ((component == NULL) || (slot_out == NULL)) {
        return 0;
    }

    if (component_capability_find(component, object, &slot)) {
        component->capabilities[slot].rights |= rights;
        *slot_out = slot;
        return 1;
    }

    if (component->capability_count >= COMPONENT_CAP_SLOTS) {
        return 0;
    }

    slot = component->capability_count;
    component->capabilities[slot].object = object;
    component->capabilities[slot].rights = rights;
    component->capabilities[slot].badge = badge;
    component->capability_count = slot + 1;

    *slot_out = slot;
    return 1;
}

int component_registry_load(
    struct object_store *store,
    struct object_id *roots_out,
    size_t capacity,
    size_t *count_out,
    uint64_t *next_sequence_out
) {
    (void)store;
    (void)roots_out;
    (void)capacity;
    (void)count_out;
    (void)next_sequence_out;
    return 0;
}

int component_registry_store(
    struct object_store *store,
    const struct object_id *roots,
    size_t count,
    uint64_t next_sequence
) {
    (void)store;
    (void)roots;
    (void)count;
    (void)next_sequence;
    return 0;
}

int component_install(
    struct object_store *store,
    const uint8_t *module_bytes,
    size_t module_size,
    struct component *component_out
) {
    (void)store;
    (void)module_bytes;
    (void)module_size;
    (void)component_out;
    return 0;
}

int component_resume(
    struct object_store *store,
    struct object_id root_id,
    struct component *component_out
) {
    (void)store;
    (void)root_id;
    (void)component_out;
    return 0;
}

int component_commit(
    struct object_store *store,
    struct component *component
) {
    (void)store;
    (void)component;
    return 0;
}

int component_invoke_timer(struct component *component) {
    (void)component;
    return 0;
}

int component_release(struct component *component) {
    if (component == NULL) {
        return 0;
    }

    memset(component, 0, sizeof(*component));
    return 1;
}
