#include "record.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../wasm/instance_state.h"
#include "../wasm/runtime.h"
#include "../cap/domain.h"

static struct replay_session *active;
void replay_set_current(struct replay_session *s) { active = s; }
struct replay_session *replay_current(void) { return active; }

int replay_start(struct replay_session *s, struct component *owner) {
    uint8_t *memory;
    size_t size, written;
    if (s == NULL || owner == NULL || owner->store == NULL ||
        owner->root_id.high != COMPONENT_SEQUENCE_ID_HIGH || active != NULL ||
        (owner->domain != NULL && owner->domain->active) ||
        !jani_wasm_instance_memory(owner->instance, &memory, &size)) return 0;
    memset(s, 0, sizeof(*s));
    s->initial_size = instance_state_size(size, owner->mailbox_used);
    s->initial = kmalloc(s->initial_size);
    if (s->initial == NULL) return 0;
    if (!instance_state_serialize(owner, memory, size, s->initial, s->initial_size, &written)) {
        kfree(s->initial); s->initial = NULL; return 0;
    }
    s->mode = REPLAY_RECORD;
    s->owner = owner;
    s->module_id = owner->module_id;
    active = s;
    return 1;
}

void replay_stop(struct replay_session *s) {
    if (s == NULL) return;
    if (active == s) active = NULL;
    kfree(s->initial);
    kfree(s->expected);
    s->initial = NULL;
    s->expected = NULL;
    s->mode = REPLAY_OFF;
}

int replay_before(struct replay_session *s, uint32_t kind, const uint64_t args[8],
                   const uint8_t *input, size_t in_size, uint8_t *output, size_t out_size, int64_t *result) {
    if (s == NULL || s->mode == REPLAY_OFF) return 1;
    if (s->failed || s->used > REPLAY_BYTES || kind > 32 ||
        (s->mode != REPLAY_RECORD && s->mode != REPLAY_PLAY) ||
        args == NULL || result == NULL || in_size > REPLAY_IO_MAX || out_size > REPLAY_IO_MAX ||
        (in_size && input == NULL) || (out_size && output == NULL)) goto fail;
    size_t needed = sizeof(struct replay_record) + in_size + out_size;
    if (s->mode == REPLAY_RECORD) {
        if (needed > REPLAY_BYTES - s->used || s->sequence == UINT64_MAX) goto fail;
        return 1;
    }
    if (s->cursor > s->used || s->used - s->cursor < sizeof(struct replay_record)) goto fail;
    struct replay_record record;
    memcpy(&record, s->bytes + s->cursor, sizeof(record));
    if (needed > s->used - s->cursor || record.kind != kind || record.sequence != s->sequence ||
        record.input_size != in_size || record.output_size != out_size || record.reserved != 0) goto fail;
    for (uint32_t i = 0; i < 8; i++) if (args[i] != record.arguments[i]) goto fail;
    const uint8_t *saved_input = s->bytes + s->cursor + sizeof(record);
    for (size_t i = 0; i < in_size; i++) if (input[i] != saved_input[i]) goto fail;
    if (out_size) memcpy(output, saved_input + in_size, out_size);
    *result = record.result;
    s->cursor += needed;
    s->sequence++;
    return 0;
fail:
    s->failed = 1;
    return -1;
}

int replay_after(struct replay_session *s, uint32_t kind, const uint64_t args[8],
                  const uint8_t *input, size_t in_size, const uint8_t *output, size_t out_size, int64_t result) {
    if (s == NULL || s->mode != REPLAY_RECORD) return 1;
    if (s->failed || s->used > REPLAY_BYTES || s->sequence == UINT64_MAX || kind > 32 || args == NULL ||
        (in_size && input == NULL) || (out_size && output == NULL) ||
        in_size > REPLAY_IO_MAX || out_size > REPLAY_IO_MAX ||
        in_size + out_size + sizeof(struct replay_record) > REPLAY_BYTES - s->used) {
        s->failed = 1; return 0;
    }
    struct replay_record record = {0};
    record.kind = kind;
    record.sequence = s->sequence++;
    memcpy(record.arguments, args, sizeof(record.arguments));
    record.result = result;
    record.input_size = (uint32_t)in_size;
    record.output_size = (uint32_t)out_size;
    memcpy(s->bytes + s->used, &record, sizeof(record));
    s->used += sizeof(record);
    if (in_size) memcpy(s->bytes + s->used, input, in_size);
    s->used += in_size;
    if (out_size) memcpy(s->bytes + s->used, output, out_size);
    s->used += out_size;
    return 1;
}

int replay_handler(struct component *owner, uint64_t now, uint32_t timer) {
    uint64_t args[8] = {now, timer};
    int64_t result = 0;
    if (active == NULL || active->owner != owner) return 1;
    int state = replay_before(active, 0, args, NULL, 0, NULL, 0, &result);
    return state >= 0 && (state == 0 || replay_after(active, 0, args, NULL, 0, NULL, 0, 0));
}

int replay_verify(struct replay_session *s) {
    struct component copy;
    uint8_t *memory, *expected;
    size_t memory_size, expected_size, module_size;
    struct object_header header;
    const uint8_t *module;
    if (s == NULL || active != s || s->owner == NULL || s->initial == NULL ||
        s->mode != REPLAY_RECORD || s->failed || s->used == 0 ||
        !replay_log_validate(s->bytes, s->used)) return 0;
    if (!object_store_get(s->owner->store, s->module_id, &header, &module, &module_size) ||
        !object_id_equal(header.type_id, (struct object_id){0, COMPONENT_TYPE_MODULE})) return 0;
    copy = *s->owner;
    copy.module = copy.instance = copy.exec_env = copy.module_bytes = NULL;
    copy.domain = NULL;
    if (!jani_wasm_instance_create(module, module_size, &copy.module, &copy.instance, &copy.exec_env, &copy.module_bytes)) return 0;
    int ok = 0;
    size_t needed;
    if (!instance_state_header_validate(s->initial, s->initial_size, NULL, &needed) ||
        !jani_wasm_instance_memory_grow(copy.instance, needed) ||
        !jani_wasm_instance_memory(copy.instance, &memory, &memory_size) ||
        !instance_state_deserialize(&copy, memory, memory_size, s->initial, s->initial_size)) goto done;
    struct component *owner = s->owner;
    s->owner = &copy;
    s->mode = REPLAY_PLAY;
    s->cursor = 0;
    s->sequence = 0;
    while (s->cursor < s->used) {
        struct replay_record record;
        memcpy(&record, s->bytes + s->cursor, sizeof(record));
        if (record.kind != 0 || record.arguments[1] > 1) { s->failed = 1; break; }
        copy.logical_time = record.arguments[0];
        if (!replay_handler(&copy, copy.logical_time, (uint32_t)record.arguments[1])) break;
        jani_wasm_set_current_component(&copy);
        if (!jani_wasm_instance_call(copy.instance, copy.exec_env,
            record.arguments[1] ? "jani_on_timer" : "jani_on_message")) { s->failed = 1; break; }
    }
    s->owner = owner;
    int have_expected;
    if (s->expected != NULL) {
        size_t offset;
        have_expected = instance_state_header_validate(s->expected, s->expected_size, &offset, &expected_size);
        expected = s->expected + (have_expected ? offset : 0);
    } else have_expected = jani_wasm_instance_memory(owner->instance, &expected, &expected_size);
    if (!s->failed && s->cursor == s->used && have_expected &&
        jani_wasm_instance_memory(copy.instance, &memory, &memory_size) && memory_size == expected_size) {
        ok = 1;
#if !defined(JANI_HOSTED) || !defined(JANI_PHASE4_BUG_REPLAY)
        for (size_t i = 0; i < memory_size; i++) if (memory[i] != expected[i]) { ok = 0; break; }
#endif
    }
    s->mode = REPLAY_RECORD;
done:
    jani_wasm_instance_destroy(copy.module, copy.instance, copy.exec_env, copy.module_bytes);
    jani_wasm_set_current_component(NULL);
    return ok;
}

int replay_save(struct replay_session *s, struct object_store *store) {
    uint8_t *memory;
    size_t size, written, module_size;
    struct object_header h;
    const uint8_t *module;
    if (s == NULL || store == NULL || s->owner == NULL || store != s->owner->store ||
        s->failed || s->initial == NULL || s->expected != NULL ||
        !object_id_equal(s->module_id, s->owner->module_id) ||
        (s->owner->domain != NULL && s->owner->domain->active) ||
        !replay_log_validate(s->bytes, s->used) ||
        !jani_wasm_instance_memory(s->owner->instance, &memory, &size) ||
        !object_store_get(store, s->module_id, &h, &module, &module_size)) return 0;
    size_t needed = instance_state_size(size, s->owner->mailbox_used);
    uint8_t *final = kmalloc(needed), *module_copy = kmalloc(module_size);
    if (final == NULL || module_copy == NULL ||
        !instance_state_serialize(s->owner, memory, size, final, needed, &written)) {
        kfree(final); kfree(module_copy); return 0;
    }
    memcpy(module_copy, module, module_size);
    struct object_store_put_request requests[4] = {
        {{7, 0}, {0, 10}, s->owner->root_id, s->owner->root_id, 0, s->initial, s->initial_size},
        {{7, 1}, {0, 11}, s->owner->root_id, s->owner->root_id, 0, s->bytes, s->used},
        {{7, 2}, {0, COMPONENT_TYPE_MODULE}, s->owner->root_id, s->owner->root_id, 0, module_copy, module_size},
        {{7, 3}, {0, 10}, s->owner->root_id, s->owner->root_id, 0, final, written}
    };
    enum object_store_batch_result result = object_store_put_many(store, requests, 4);
    if (result == OBJECT_STORE_BATCH_RECOVERY_REQUIRED && s->owner->domain != NULL) s->owner->domain->halted = 1;
    kfree(final);
    kfree(module_copy);
    return result == OBJECT_STORE_BATCH_COMMITTED;
}

int replay_load(struct replay_session *s, struct component *owner) {
    struct object_header h;
    const uint8_t *bytes;
    size_t size;
    if (s == NULL || owner == NULL || owner->store == NULL || active != NULL) return 0;
    memset(s, 0, sizeof(*s));
    for (uint32_t i = 0; i < 4; i++) {
        uint64_t type = i == 1 ? 11 : (i == 2 ? COMPONENT_TYPE_MODULE : 10);
        if (!object_store_get(owner->store, (struct object_id){7, i}, &h, &bytes, &size) ||
            !object_id_equal(h.type_id, (struct object_id){0, type}) ||
            !object_id_equal(h.creator_id, owner->root_id)) goto fail;
        if (i == 2) continue;
        if (i == 1) {
            if (!replay_log_validate(bytes, size)) goto fail;
            memcpy(s->bytes, bytes, size);
            s->used = size;
        } else {
            if (!instance_state_header_validate(bytes, size, NULL, NULL)) goto fail;
            uint8_t *copy = kmalloc(size);
            if (copy == NULL) goto fail;
            memcpy(copy, bytes, size);
            if (i == 0) { s->initial = copy; s->initial_size = size; }
            else { s->expected = copy; s->expected_size = size; }
        }
    }
    s->owner = owner;
    s->module_id = (struct object_id){7, 2};
    s->mode = REPLAY_RECORD;
    active = s;
    return 1;
fail:
    replay_stop(s);
    return 0;
}
