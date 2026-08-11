#include "component.h"

#include "../lib/string.h"
#include "../mm/heap.h"
#include "../obj/object_header.h"
#include "instance_state.h"
#include "runtime.h"

#include "generated/records_conform.h"

struct object_id component_make_id(uint64_t high, uint64_t low) {
    struct object_id id;

    id.high = high;
    id.low = low;
    return id;
}

int component_capability_find(
    const struct component *component,
    struct object_id object,
    uint32_t *slot_out)
{
    if (component == NULL) {
        return 0;
    }

    return capability_table_find(
        &component->capability_table,
        object,
        slot_out
    );
}

int component_capability_insert(
    struct component *component,
    struct object_id object,
    uint32_t rights,
    uint32_t badge,
    uint32_t *slot_out)
{
    struct capability capability;
    struct capability previous;
    uint32_t slot;

    if (component == NULL || slot_out == NULL ||
        !capability_table_is_valid(&component->capability_table)) {
        return 0;
    }

    if (capability_table_find(
            &component->capability_table,
            object,
            &slot)) {
        previous = component->capability_table.slots[slot];
        capability = previous;
        capability.rights |= rights;

        if (!capability_is_valid(&capability)) {
            return 0;
        }

        component->capability_table.slots[slot] = capability;
        if (!capability_table_is_valid(&component->capability_table)) {
            component->capability_table.slots[slot] = previous;
            return 0;
        }
    } else {
        capability.object = object;
        capability.rights = rights;
        capability.badge = badge;

        if (!capability_table_insert_root(
                &component->capability_table,
                &capability,
                &slot)) {
            return 0;
        }

        if (slot >= component->capability_count) {
            component->capability_count = slot + 1;
        }
    }

    component->capabilities_dirty = 1;
    *slot_out = slot;
    return 1;
}

int component_capability_derive(
    struct component *component,
    uint32_t parent_slot,
    uint32_t child_rights,
    uint32_t child_badge,
    uint32_t *child_slot_out)
{
    uint32_t child_slot;

    if (component == NULL || child_slot_out == NULL ||
        !capability_table_is_valid(&component->capability_table) ||
        parent_slot >= component->capability_count) {
        return 0;
    }

    if (!capability_table_derive(
            &component->capability_table,
            parent_slot,
            child_rights,
            child_badge,
            &child_slot)) {
        return 0;
    }

    if (child_slot >= component->capability_count) {
        component->capability_count = child_slot + 1;
    }

    component->capabilities_dirty = 1;
    *child_slot_out = child_slot;
    return 1;
}

int component_capability_revoke(
    struct component *component,
    uint32_t slot)
{
    if (component == NULL ||
        !capability_table_is_valid(&component->capability_table) ||
        slot >= component->capability_count) {
        return 0;
    }

    if (!capability_table_revoke(&component->capability_table, slot)) {
        return 0;
    }

    while (component->capability_count > 0) {
        uint32_t last_slot = component->capability_count - 1;

        if (capability_table_get(
                &component->capability_table,
                last_slot) != NULL) {
            break;
        }

        component->capability_count--;
    }

    component->capabilities_dirty = 1;
    return 1;
}

static struct object_id component_sequence_id(uint64_t sequence) {
    return component_make_id(COMPONENT_SEQUENCE_ID_HIGH, sequence);
}

#define CAPTABLE_CAPABILITIES_BYTES \
    (COMPONENT_CAP_SLOTS * sizeof(struct capability))
#define CAPTABLE_PARENTS_BYTES \
    (COMPONENT_CAP_SLOTS * sizeof(uint32_t))
#define CAPTABLE_V1_PAYLOAD_BYTES \
    (COMPONENT_CAPTABLE_HEADER_SIZE + \
     CAPTABLE_CAPABILITIES_BYTES)
#define CAPTABLE_PAYLOAD_BYTES \
    (CAPTABLE_V1_PAYLOAD_BYTES + CAPTABLE_PARENTS_BYTES)

static void component_capability_parents_clear(struct component *component) {
    uint32_t slot;

    for (slot = 0; slot < COMPONENT_CAP_SLOTS; slot++) {
        component->capability_table.parents[slot] = COMPONENT_CAP_PARENT_NONE;
    }
}

static int component_capability_table_valid(
    const struct component *component,
    uint32_t slot_count
) {
    uint32_t slot;

    if (!capability_table_is_valid(&component->capability_table)) {
        return 0;
    }

    for (slot = slot_count; slot < COMPONENT_CAP_SLOTS; slot++) {
        if (capability_table_get(&component->capability_table, slot) != NULL) {
            return 0;
        }
    }

    return 1;
}

int component_captable_write(
    struct object_store *store,
    struct component *component
) {
    uint8_t buffer[CAPTABLE_PAYLOAD_BYTES];
    struct component_captable_header captable;

    if ((store == NULL) || (component == NULL)) {
        return 0;
    }
    if ((component->capability_count > COMPONENT_CAP_SLOTS) ||
        !component_capability_table_valid(
            component, component->capability_count
        )) {
        return 0;
    }

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer + COMPONENT_CAPTABLE_HEADER_SIZE,
           component->capability_table.slots,
           sizeof(component->capability_table.slots));
    for (uint32_t slot = 0; slot < COMPONENT_CAP_SLOTS; slot++) {
        uint32_t parent = component->capability_table.parents[slot];

        if (object_id_is_zero(
                component->capability_table.slots[slot].object)) {
            parent = COMPONENT_CAP_PARENT_NONE;
        }
        memcpy(buffer + CAPTABLE_V1_PAYLOAD_BYTES +
                   (slot * sizeof(parent)),
               &parent, sizeof(parent));
    }

    memset(&captable, 0, sizeof(captable));
    captable.magic = COMPONENT_CAPTABLE_MAGIC;
    captable.format_version = COMPONENT_CAPTABLE_FORMAT_VERSION;
    captable.slot_count = component->capability_count;
    captable.next_object_sequence = component->next_object_sequence;
    captable.payload_crc32c = object_crc32c(
        buffer + COMPONENT_CAPTABLE_HEADER_SIZE,
        CAPTABLE_CAPABILITIES_BYTES + CAPTABLE_PARENTS_BYTES
    );

    memcpy(buffer, &captable, COMPONENT_CAPTABLE_HEADER_SIZE);

    if (!object_store_put(store, component->captable_id,
                          component_make_id(0, COMPONENT_TYPE_CAPTABLE),
                          component->root_id, component->root_id,
                          component->logical_time, buffer, sizeof(buffer))) {
        return 0;
    }

    component->capabilities_dirty = 0;
    return 1;
}

int component_captable_read(
    struct object_store *store,
    struct component *component
) {
    struct component_captable_header captable;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    size_t table_bytes;

    if ((store == NULL) || (component == NULL)) {
        return 0;
    }

    if (!object_store_get(store, component->captable_id, &header, &payload,
                          &payload_size)) {
        return 0;
    }

    if (payload_size < COMPONENT_CAPTABLE_HEADER_SIZE) {
        return 0;
    }

    memcpy(&captable, payload, COMPONENT_CAPTABLE_HEADER_SIZE);

    if ((captable.magic != COMPONENT_CAPTABLE_MAGIC) ||
        (captable._reserved != 0) ||
        (captable.slot_count > COMPONENT_CAP_SLOTS)) {
        return 0;
    }

    if (captable.format_version == COMPONENT_CAPTABLE_FORMAT_VERSION_V1) {
        if (payload_size != CAPTABLE_V1_PAYLOAD_BYTES) {
            return 0;
        }
        table_bytes = CAPTABLE_CAPABILITIES_BYTES;
    } else if (captable.format_version == COMPONENT_CAPTABLE_FORMAT_VERSION) {
        if (payload_size != CAPTABLE_PAYLOAD_BYTES) {
            return 0;
        }
        table_bytes = CAPTABLE_CAPABILITIES_BYTES + CAPTABLE_PARENTS_BYTES;
    } else {
        return 0;
    }

    if (object_crc32c(payload + COMPONENT_CAPTABLE_HEADER_SIZE,
                      table_bytes) !=
        captable.payload_crc32c) {
        return 0;
    }

    memcpy(component->capability_table.slots,
           payload + COMPONENT_CAPTABLE_HEADER_SIZE,
           sizeof(component->capability_table.slots));
    if (captable.format_version == COMPONENT_CAPTABLE_FORMAT_VERSION_V1) {
        component_capability_parents_clear(component);
    } else {
        memcpy(component->capability_table.parents,
               payload + CAPTABLE_V1_PAYLOAD_BYTES,
               sizeof(component->capability_table.parents));
    }
    if (!component_capability_table_valid(component, captable.slot_count)) {
        return 0;
    }
    component->capability_count = captable.slot_count;
    component->next_object_sequence = captable.next_object_sequence;
    component->capabilities_dirty = 0;
    return 1;
}

int component_mailbox_push(
    struct component *component,
    const uint8_t *bytes,
    uint32_t length,
    int32_t capability_slot
) {
    uint32_t header[2];
    uint32_t needed;

    if ((component == NULL) || ((length != 0) && (bytes == NULL))) {
        return 0;
    }

    needed = (uint32_t)sizeof(header) + length;
    if (needed > (COMPONENT_MAILBOX_BYTES - component->mailbox_used)) {
        return 0;
    }

    header[0] = length;
    header[1] = (uint32_t)capability_slot;

    memcpy(component->mailbox + component->mailbox_used, header,
           sizeof(header));
    if (length != 0) {
        memcpy(component->mailbox + component->mailbox_used + sizeof(header),
               bytes, length);
    }

    component->mailbox_used += needed;
    return 1;
}

int component_mailbox_pop(
    struct component *component,
    uint8_t *bytes_out,
    uint32_t capacity,
    uint32_t *length_out,
    int32_t *capability_slot_out
) {
    uint32_t header[2];
    uint32_t frame;

    if ((component == NULL) || (length_out == NULL) ||
        (capability_slot_out == NULL)) {
        return 0;
    }

    if (component->mailbox_used < sizeof(header)) {
        return 0;
    }

    memcpy(header, component->mailbox, sizeof(header));
    frame = (uint32_t)sizeof(header) + header[0];

    if ((frame > component->mailbox_used) || (header[0] > capacity)) {
        return 0;
    }

    if ((header[0] != 0) && (bytes_out != NULL)) {
        memcpy(bytes_out, component->mailbox + sizeof(header), header[0]);
    }

    component->mailbox_used -= frame;
    if (component->mailbox_used != 0) {
        memmove(component->mailbox, component->mailbox + frame,
                component->mailbox_used);
    }

    *length_out = header[0];
    *capability_slot_out = (int32_t)header[1];
    return 1;
}

static uint8_t *component_scratch;
static size_t component_scratch_size;

static uint8_t *component_scratch_reserve(size_t needed) {
    uint8_t *grown;

    if ((component_scratch != NULL) && (component_scratch_size >= needed)) {
        return component_scratch;
    }

    grown = kmalloc(needed);
    if (grown == NULL) {
        return NULL;
    }

    if (component_scratch != NULL) {
        kfree(component_scratch);
    }

    component_scratch = grown;
    component_scratch_size = needed;
    return component_scratch;
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
    scratch = component_scratch_reserve(needed);
    if (scratch == NULL) {
        return 0;
    }

    if (component->capabilities_dirty &&
        !component_captable_write(store, component)) {
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
    uint32_t root_slot;

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
    capability_table_init(&component_out->capability_table);
    component_out->module_id = component_sequence_id(sequence);
    component_out->captable_id = component_sequence_id(sequence + 1);
    component_out->state_id = component_sequence_id(sequence + 2);
    component_out->root_id = component_sequence_id(sequence + 3);
    component_out->store = store;
    component_out->next_object_sequence = 1;

    if (!component_capability_insert(component_out, component_out->root_id,
                                     COMPONENT_RIGHTS_READ |
                                     COMPONENT_RIGHTS_SEND,
                                     0, &root_slot)) {
        return 0;
    }

    if (!object_store_put(store, component_out->module_id,
                          component_make_id(0, COMPONENT_TYPE_MODULE),
                          component_out->root_id, component_out->root_id, 0,
                          module_bytes, module_size)) {
        return 0;
    }

    if (!component_captable_write(store, component_out)) {
        return 0;
    }

    if (!jani_wasm_instance_create(module_bytes, module_size,
                                   &component_out->module,
                                   &component_out->instance,
                                   &component_out->exec_env,
                                   &component_out->module_bytes)) {
        return 0;
    }

    jani_wasm_set_current_component(component_out);
    if (!jani_wasm_instance_call(component_out->instance,
                                 component_out->exec_env, "jani_init")) {
        component_release(component_out);
        return 0;
    }

    if (!component_commit(store, component_out)) {
        component_release(component_out);
        return 0;
    }

    if (!component_root_write(store, component_out)) {
        component_release(component_out);
        return 0;
    }

    roots[count] = component_out->root_id;
    if (!component_registry_store(store, roots, count + 1, sequence + 4)) {
        component_release(component_out);
        return 0;
    }

    return 1;
}

int component_resume(
    struct object_store *store,
    struct object_id root_id,
    struct component *component_out
) {
    struct object_header header;
    const uint8_t *payload;
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
    created = jani_wasm_instance_create(payload, module_size,
                                        &component_out->module,
                                        &component_out->instance,
                                        &component_out->exec_env,
                                        &component_out->module_bytes);
    if (!created) {
        return 0;
    }

    if (!object_store_get(store, component_out->state_id, &header, &payload,
                          &payload_size) ||
        !instance_state_header_validate(payload, payload_size, NULL,
                                        &saved_memory_size) ||
        !jani_wasm_instance_memory_grow(component_out->instance,
                                        saved_memory_size) ||
        !jani_wasm_instance_memory(component_out->instance, &memory,
                                   &memory_size) ||
        !instance_state_deserialize(component_out, memory, memory_size,
                                    payload, payload_size)) {
        component_release(component_out);
        return 0;
    }

    if (!component_captable_read(store, component_out)) {
        component_release(component_out);
        return 0;
    }

    component_out->store = store;
    return 1;
}

int component_uninstall(
    struct object_store *store,
    struct object_id root_id
) {
    struct object_id roots[COMPONENT_MAX];
    struct component component;
    uint64_t sequence;
    size_t count;
    size_t index;

    if ((store == NULL) || object_id_is_zero(root_id)) {
        return 0;
    }

    if (!component_registry_load(store, roots, COMPONENT_MAX, &count,
                                 &sequence)) {
        return 0;
    }

    for (index = 0; index < count; index++) {
        if (object_id_equal(roots[index], root_id)) {
            break;
        }
    }
    if (index == count) {
        return 0;
    }

    memset(&component, 0, sizeof(component));
    if (!component_root_read(store, root_id, &component)) {
        return 0;
    }

    while ((index + 1) < count) {
        roots[index] = roots[index + 1];
        index++;
    }
    count--;

    if (!component_registry_store(store, roots, count, sequence)) {
        return 0;
    }

    return object_store_delete(store, component.module_id);
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

    jani_wasm_instance_destroy(component->module, component->instance,
                               component->exec_env, component->module_bytes);
    jani_wasm_set_current_component(NULL);

    memset(component, 0, sizeof(*component));
    return 1;
}
