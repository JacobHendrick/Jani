#include "component.h"

#include "../lib/string.h"
#include "../mm/heap.h"
#include "../obj/object_header.h"
#include "instance_state.h"
#include "runtime.h"

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

static struct object_id component_sequence_id(uint64_t sequence) {
    return component_make_id(COMPONENT_SEQUENCE_ID_HIGH, sequence);
}

int component_registry_load(
    struct object_store *store,
    struct object_id *roots_out,
    size_t capacity,
    size_t *count_out,
    uint64_t *next_sequence_out
) {
    struct component_registry_header registry;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    size_t expected;
    size_t index;

    if ((store == NULL) || (count_out == NULL) ||
        (next_sequence_out == NULL)) {
        return 0;
    }

    *count_out = 0;
    *next_sequence_out = 1;

    if (!object_store_get(store,
                          component_make_id(COMPONENT_REGISTRY_ID_HIGH,
                                            COMPONENT_REGISTRY_ID_LOW),
                          &header, &payload, &payload_size)) {
        return 0;
    }

    if (payload_size < COMPONENT_REGISTRY_HEADER_SIZE) {
        return 0;
    }

    memcpy(&registry, payload, COMPONENT_REGISTRY_HEADER_SIZE);

    if ((registry.magic != COMPONENT_REGISTRY_MAGIC) ||
        (registry.format_version != COMPONENT_REGISTRY_FORMAT_VERSION) ||
        (registry._reserved != 0) ||
        (registry.component_count > COMPONENT_MAX)) {
        return 0;
    }

    expected = COMPONENT_REGISTRY_HEADER_SIZE +
               ((size_t)registry.component_count * sizeof(struct object_id));
    if (payload_size != expected) {
        return 0;
    }

    if (object_crc32c(payload + COMPONENT_REGISTRY_HEADER_SIZE,
                      payload_size - COMPONENT_REGISTRY_HEADER_SIZE) !=
        registry.payload_crc32c) {
        return 0;
    }

    if ((size_t)registry.component_count > capacity) {
        return 0;
    }

    for (index = 0; index < registry.component_count; index++) {
        memcpy(&roots_out[index],
               payload + COMPONENT_REGISTRY_HEADER_SIZE +
                   (index * sizeof(struct object_id)),
               sizeof(struct object_id));
    }

    *count_out = registry.component_count;
    *next_sequence_out = registry.next_sequence;
    return 1;
}

int component_registry_store(
    struct object_store *store,
    const struct object_id *roots,
    size_t count,
    uint64_t next_sequence
) {
    uint8_t buffer[COMPONENT_REGISTRY_HEADER_SIZE +
                   (COMPONENT_MAX * sizeof(struct object_id))];
    struct component_registry_header registry;
    size_t total;
    size_t index;

    if ((store == NULL) || (count > COMPONENT_MAX)) {
        return 0;
    }
    if ((count != 0) && (roots == NULL)) {
        return 0;
    }

    total = COMPONENT_REGISTRY_HEADER_SIZE +
            (count * sizeof(struct object_id));
    memset(buffer, 0, sizeof(buffer));

    for (index = 0; index < count; index++) {
        memcpy(buffer + COMPONENT_REGISTRY_HEADER_SIZE +
                   (index * sizeof(struct object_id)),
               &roots[index], sizeof(struct object_id));
    }

    memset(&registry, 0, sizeof(registry));
    registry.magic = COMPONENT_REGISTRY_MAGIC;
    registry.format_version = COMPONENT_REGISTRY_FORMAT_VERSION;
    registry.component_count = (uint32_t)count;
    registry.next_sequence = next_sequence;
    registry.payload_crc32c = object_crc32c(
        buffer + COMPONENT_REGISTRY_HEADER_SIZE,
        total - COMPONENT_REGISTRY_HEADER_SIZE
    );

    memcpy(buffer, &registry, COMPONENT_REGISTRY_HEADER_SIZE);

    return object_store_put(
        store,
        component_make_id(COMPONENT_REGISTRY_ID_HIGH,
                          COMPONENT_REGISTRY_ID_LOW),
        component_make_id(0, COMPONENT_TYPE_REGISTRY),
        component_make_id(0, 0), component_make_id(0, 0), 0,
        buffer, total
    );
}

static int component_root_write(
    struct object_store *store,
    const struct component *component
) {
    struct component_root_record record;

    memset(&record, 0, sizeof(record));
    record.magic = COMPONENT_ROOT_MAGIC;
    record.format_version = COMPONENT_ROOT_FORMAT_VERSION;
    record.module_id = component->module_id;
    record.captable_id = component->captable_id;
    record.state_id = component->state_id;
    record.payload_crc32c = object_crc32c(
        (const uint8_t *)&record.module_id,
        3 * sizeof(struct object_id)
    );

    return object_store_put(
        store, component->root_id,
        component_make_id(0, COMPONENT_TYPE_ROOT),
        component_make_id(0, 0), component_make_id(0, 0), 0,
        (const uint8_t *)&record, sizeof(record)
    );
}

static int component_root_read(
    struct object_store *store,
    struct object_id root_id,
    struct component *component
) {
    struct component_root_record record;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;

    if (!object_store_get(store, root_id, &header, &payload, &payload_size)) {
        return 0;
    }
    if (payload_size != sizeof(record)) {
        return 0;
    }

    memcpy(&record, payload, sizeof(record));

    if ((record.magic != COMPONENT_ROOT_MAGIC) ||
        (record.format_version != COMPONENT_ROOT_FORMAT_VERSION) ||
        (record._reserved != 0) || (record._padding != 0)) {
        return 0;
    }

    if (object_crc32c((const uint8_t *)&record.module_id,
                      3 * sizeof(struct object_id)) !=
        record.payload_crc32c) {
        return 0;
    }

    component->root_id = root_id;
    component->module_id = record.module_id;
    component->captable_id = record.captable_id;
    component->state_id = record.state_id;
    return 1;
}

int component_commit(
    struct object_store *store,
    struct component *component
) {
    uint8_t *memory;
    uint8_t *scratch;
    size_t memory_size;
    size_t needed;
    size_t written;
    int result;

    if ((store == NULL) || (component == NULL)) {
        return 0;
    }

    if (!jani_wasm_instance_memory(component->instance, &memory,
                                   &memory_size)) {
        return 0;
    }

    needed = instance_state_size(memory_size, component->mailbox_used);
    scratch = kmalloc(needed);
    if (scratch == NULL) {
        return 0;
    }

    result = 0;
    if (instance_state_serialize(component, memory, memory_size, scratch,
                                 needed, &written)) {
        result = object_store_put(
            store, component->state_id,
            component_make_id(0, COMPONENT_TYPE_INSTANCE_STATE),
            component->root_id, component->root_id,
            component->logical_time, scratch, written
        );
    }

    kfree(scratch);
    return result;
}

int component_install(
    struct object_store *store,
    const uint8_t *module_bytes,
    size_t module_size,
    struct component *component_out
) {
    struct object_id roots[COMPONENT_MAX];
    uint64_t sequence;
    size_t count;

    if ((store == NULL) || (module_bytes == NULL) ||
        (component_out == NULL) || (module_size == 0)) {
        return 0;
    }

    if (!component_registry_load(store, roots, COMPONENT_MAX, &count,
                                 &sequence)) {
        count = 0;
        sequence = 1;
    }
    if (count >= COMPONENT_MAX) {
        return 0;
    }

    memset(component_out, 0, sizeof(*component_out));
    component_out->module_id = component_sequence_id(sequence);
    component_out->captable_id = component_sequence_id(sequence + 1);
    component_out->state_id = component_sequence_id(sequence + 2);
    component_out->root_id = component_sequence_id(sequence + 3);

    if (!object_store_put(store, component_out->module_id,
                          component_make_id(0, COMPONENT_TYPE_MODULE),
                          component_out->root_id, component_out->root_id, 0,
                          module_bytes, module_size)) {
        return 0;
    }

    if (!object_store_put(store, component_out->captable_id,
                          component_make_id(0, COMPONENT_TYPE_CAPTABLE),
                          component_out->root_id, component_out->root_id, 0,
                          (const uint8_t *)component_out->capabilities,
                          sizeof(component_out->capabilities))) {
        return 0;
    }

    if (!jani_wasm_instance_create(module_bytes, module_size,
                                   &component_out->module,
                                   &component_out->instance,
                                   &component_out->exec_env)) {
        return 0;
    }

    jani_wasm_set_current_component(component_out);
    if (!jani_wasm_instance_call(component_out->instance,
                                 component_out->exec_env, "jani_init")) {
        return 0;
    }

    if (!component_commit(store, component_out)) {
        return 0;
    }

    if (!component_root_write(store, component_out)) {
        return 0;
    }

    roots[count] = component_out->root_id;
    return component_registry_store(store, roots, count + 1, sequence + 4);
}

int component_resume(
    struct object_store *store,
    struct object_id root_id,
    struct component *component_out
) {
    struct object_header header;
    const uint8_t *payload;
    uint8_t *module_copy;
    uint8_t *memory;
    size_t payload_size;
    size_t module_size;
    size_t memory_size;
    size_t saved_memory_size;
    int created;

    if ((store == NULL) || (component_out == NULL)) {
        return 0;
    }

    memset(component_out, 0, sizeof(*component_out));

    if (!component_root_read(store, root_id, component_out)) {
        return 0;
    }

    if (!object_store_get(store, component_out->module_id, &header, &payload,
                          &payload_size)) {
        return 0;
    }

    module_size = payload_size;
    module_copy = kmalloc(module_size);
    if (module_copy == NULL) {
        return 0;
    }
    memcpy(module_copy, payload, module_size);

    created = jani_wasm_instance_create(module_copy, module_size,
                                        &component_out->module,
                                        &component_out->instance,
                                        &component_out->exec_env);
    kfree(module_copy);

    if (!created) {
        return 0;
    }

    if (!object_store_get(store, component_out->state_id, &header, &payload,
                          &payload_size)) {
        return 0;
    }

    if (!instance_state_header_validate(payload, payload_size, NULL,
                                        &saved_memory_size)) {
        return 0;
    }

    if (!jani_wasm_instance_memory_grow(component_out->instance,
                                        saved_memory_size)) {
        return 0;
    }

    if (!jani_wasm_instance_memory(component_out->instance, &memory,
                                   &memory_size)) {
        return 0;
    }

    return instance_state_deserialize(component_out, memory, memory_size,
                                      payload, payload_size);
}

int component_invoke_timer(struct component *component) {
    if ((component == NULL) || (component->instance == NULL)) {
        return 0;
    }

    component->logical_time += 1;
    component->timer_armed = 0;
    jani_wasm_set_current_component(component);

    return jani_wasm_instance_call(component->instance, component->exec_env,
                                   "jani_on_timer");
}

int component_release(struct component *component) {
    if (component == NULL) {
        return 0;
    }

    memset(component, 0, sizeof(*component));
    return 1;
}
