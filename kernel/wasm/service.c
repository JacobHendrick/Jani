#include "service.h"
#include "instance_state.h"
#include "runtime.h"
#include "../cap/domain.h"
#include "../sched/scheduler.h"
#include "../replay/record.h"
#include "../lib/string.h"
#include "../mm/heap.h"

static struct object_id binding_id(const struct component *c) { return (struct object_id){8, c->root_id.low}; }

static void checksum(struct service_binding *binding) {
    binding->crc = object_crc32c((const uint8_t *)binding, 104);
}

static int load_binding(struct component *c, struct service_binding *binding) {
    struct object_header header;
    const uint8_t *bytes;
    size_t size;
    if (!object_store_get(c->store, binding_id(c), &header, &bytes, &size) ||
        !object_id_equal(header.type_id, (struct object_id){0, 12}) ||
        !service_binding_validate(bytes, size)) return 0;
    memcpy(binding, bytes, sizeof(*binding));
    return object_id_equal(binding->owner, c->root_id) && object_id_equal(binding->module, c->module_id);
}

int service_bind(struct component *c, uint64_t schema, uint64_t messages,
                  uint32_t state_offset, uint32_t state_size) {
    struct service_binding b = {0};
    uint8_t *memory;
    size_t size;
    if (c == NULL || c->capabilities_dirty || c->root_id.high != COMPONENT_SEQUENCE_ID_HIGH || schema == 0 || messages == 0 ||
        (c->domain != NULL && c->domain->active) ||
        object_table_find(&c->store->table, binding_id(c)) != NULL || state_size == 0 ||
        !jani_wasm_instance_memory(c->instance, &memory, &size) || state_offset > size || state_size > size - state_offset) return 0;
    b.magic = UINT64_C(0x4A414E4953564331);
    b.format = 1;
    b.owner = c->root_id;
    b.module = c->module_id;
    b.state_schema = schema;
    b.message_mask = messages;
    b.generation = 1;
    b.state_offset = state_offset;
    b.state_size = state_size;
    checksum(&b);
    return object_store_put(c->store, binding_id(c), (struct object_id){0, 12}, c->root_id,
        c->root_id, c->logical_time, (const uint8_t *)&b, sizeof(b));
}

static int replace(struct component *c, const uint8_t *module, size_t length,
                    uint64_t schema, uint64_t messages, int rollback) {
    struct service_binding b;
    struct component candidate;
    struct component_root_record root = {0};
    uint8_t *memory, *new_memory, *state = NULL;
    size_t size, new_size, written;
    int ok = 0;
    if (c == NULL || c->exited || c->capabilities_dirty || module == NULL || length == 0 ||
        (replay_current() != NULL && replay_current()->owner == c) ||
        c->root_id.low > UINT32_MAX || (c->domain != NULL && (c->domain->active || c->domain->halted)) ||
        !load_binding(c, &b) || b.generation >= UINT32_MAX || schema != b.state_schema ||
        (rollback ? messages != b.previous_messages : (messages & b.message_mask) != b.message_mask) ||
        !jani_wasm_instance_memory(c->instance, &memory, &size)) return 0;
    candidate = *c;
#if defined(JANI_HOSTED) && defined(JANI_PHASE4_BUG_SWAP)
    candidate.mailbox_used = 0;
#endif
    candidate.module = candidate.instance = candidate.exec_env = candidate.module_bytes = NULL;
    candidate.module_id = (struct object_id){9, (c->root_id.low << 32) | (b.generation + 1)};
    if (object_table_find(&c->store->table, candidate.module_id) != NULL ||
        !jani_wasm_instance_create(module, length, &candidate.module, &candidate.instance,
                                    &candidate.exec_env, &candidate.module_bytes)) return 0;
    if (!jani_wasm_instance_has_handler(candidate.instance, "jani_on_message") ||
        !jani_wasm_instance_has_handler(candidate.instance, "jani_on_timer") ||
        !jani_wasm_instance_memory(candidate.instance, &new_memory, &new_size) ||
        b.state_offset > new_size || b.state_size > new_size - b.state_offset ||
        b.state_offset > size || b.state_size > size - b.state_offset) goto done;
    /* The declared state region moves; the new module keeps its own data segments. */
    memcpy(new_memory + b.state_offset, memory + b.state_offset, b.state_size);
    size_t needed = instance_state_size(new_size, c->mailbox_used);
    state = kmalloc(needed);
    if (state == NULL || !instance_state_serialize(&candidate, new_memory, new_size, state, needed, &written)) goto done;
    root.magic = COMPONENT_ROOT_MAGIC;
    root.format_version = COMPONENT_ROOT_FORMAT_VERSION;
    root.module_id = candidate.module_id;
    root.captable_id = c->captable_id;
    root.state_id = c->state_id;
    root.payload_crc32c = object_crc32c((const uint8_t *)&root.module_id, 48);
    b.previous_module = b.module;
    b.previous_messages = b.message_mask;
    b.module = candidate.module_id;
    b.message_mask = messages;
    b.generation++;
    checksum(&b);
    struct object_store_put_request requests[4] = {
        {candidate.module_id, {0, COMPONENT_TYPE_MODULE}, c->root_id, c->root_id, c->logical_time, module, length},
        {c->root_id, {0, COMPONENT_TYPE_ROOT}, c->root_id, c->root_id, c->logical_time, (const uint8_t *)&root, sizeof(root)},
        {c->state_id, {0, COMPONENT_TYPE_INSTANCE_STATE}, c->root_id, c->root_id, c->logical_time, state, written},
        {binding_id(c), {0, 12}, c->root_id, c->root_id, c->logical_time, (const uint8_t *)&b, sizeof(b)}
    };
    enum object_store_batch_result result = object_store_put_many(c->store, requests, 4);
    if (result != OBJECT_STORE_BATCH_COMMITTED) {
        if (result == OBJECT_STORE_BATCH_RECOVERY_REQUIRED && c->domain != NULL) c->domain->halted = 1;
        goto done;
    }
    jani_wasm_instance_destroy(c->module, c->instance, c->exec_env, c->module_bytes);
    *c = candidate;
    scheduler_trace(scheduler_current(), c, TRACE_SWAP, b.generation);
    ok = 1;
done:
    kfree(state);
    if (!ok) jani_wasm_instance_destroy(candidate.module, candidate.instance, candidate.exec_env, candidate.module_bytes);
    return ok;
}

int service_swap(struct component *owner, const uint8_t *module, size_t length,
                  uint64_t state_schema, uint64_t message_mask) {
    return replace(owner, module, length, state_schema, message_mask, 0);
}

int service_rollback(struct component *c) {
    struct service_binding b;
    struct object_header h;
    const uint8_t *bytes;
    size_t size;
    if (c == NULL || !load_binding(c, &b) || object_id_is_zero(b.previous_module) ||
        !object_store_get(c->store, b.previous_module, &h, &bytes, &size) ||
        !object_id_equal(h.type_id, (struct object_id){0, COMPONENT_TYPE_MODULE})) return 0;
    /* Copy borrowed cache bytes before subsequent store reads can evict them. */
    uint8_t *copy = kmalloc(size);
    if (copy == NULL) return 0;
    memcpy(copy, bytes, size);
    int ok = replace(c, copy, size, b.state_schema, b.previous_messages, 1);
    kfree(copy);
    return ok;
}
