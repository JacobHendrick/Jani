#include "syscalls.h"
#include "runtime.h"

#include "wasm_export.h"

#include "component.h"
#include "component_set.h"
#include "syscall_args.h"
#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../obj/object_header.h"
#include "../cap/domain.h"
#include "../sched/scheduler.h"
#include "../replay/record.h"
#include "../drivers/block_component.h"

#define jani_log_impl jani_log_live
#define jani_object_create_impl jani_object_create_live
#define jani_object_read_impl jani_object_read_live
#define jani_object_write_impl jani_object_write_live
#define jani_object_size_impl jani_object_size_live
#define jani_cap_drop_impl jani_cap_drop_live
#define jani_cap_derive_impl jani_cap_derive_live
#define jani_cap_revoke_impl jani_cap_revoke_live
#define jani_message_send_impl jani_message_send_live
#define jani_message_recv_impl jani_message_recv_live
#define jani_timer_set_impl jani_timer_set_live
#define jani_time_logical_impl jani_time_logical_live
#define jani_self_impl jani_self_live
#define jani_exit_impl jani_exit_live
#define jani_provenance_impl jani_provenance_live
#define jani_stats_impl jani_stats_live
#define jani_trace_impl jani_trace_live

#define SYSCALL_TRANSFER_MAX 4096u

static struct component *current;
static struct component_set *running_components;

static int current_read(struct object_id id, struct object_header *header,
                         const uint8_t **payload, size_t *size) {
    if (current->domain != NULL) return capability_domain_read(current->domain, id, header, payload, size);
    return object_store_get(current->store, id, header, payload, size);
}

static int current_write(struct object_id id, struct object_id type,
                          struct object_id creator, const uint8_t *bytes, size_t size) {
    struct object_store_put_request request = {id, type, creator, current->root_id,
        current->logical_time, bytes, size};
    if (current->domain != NULL) return capability_domain_write(current->domain, &request);
    return object_store_put_many(current->store, &request, 1) == OBJECT_STORE_BATCH_COMMITTED;
}

void jani_syscall_set_current(struct component *component) {
    current = component;
}

void jani_syscall_set_component_set(struct component_set *set) {
    running_components = set;
}

struct component *jani_syscall_current(void) {
    return current;
}

static uint8_t *linear_memory(
    wasm_exec_env_t exec_env,
    uint32_t offset,
    uint32_t length
) {
    wasm_module_inst_t instance;

    instance = wasm_runtime_get_module_inst(exec_env);
    if (instance == NULL) {
        return NULL;
    }

    if (!wasm_runtime_validate_app_addr(instance, (uint64_t)offset,
                                        (uint64_t)length)) {
        return NULL;
    }

    return (uint8_t *)wasm_runtime_addr_app_to_native(instance,
                                                      (uint64_t)offset);
}

static int slot_object(int32_t slot, struct object_id *object_out) {
    const struct capability *capability;

    if (current == NULL) {
        return 0;
    }

    if (!jani_syscall_check_slot(current->capability_count, slot)) {
        return 0;
    }

    capability = capability_table_get(
        &current->capability_table,
        (uint32_t)slot
    );
    if (capability == NULL) {
        return 0;
    }

    *object_out = capability->object;
    return 1;
}

static int slot_allows(int32_t slot, uint32_t rights) {
    const struct capability *capability;

    if (current->domain != NULL) {
        uint32_t operation = rights == CAP_RIGHT_WRITE ? PROVENANCE_WRITE :
            (rights == CAP_RIGHT_SEND ? PROVENANCE_SEND : PROVENANCE_READ);
        return capability_domain_use(current->domain, current, slot, rights, operation) == 0;
    }
    capability = capability_table_get(
        &current->capability_table,
        (uint32_t)slot
    );
    return capability_allows(capability, rights);
}

static int32_t jani_log_impl(
    wasm_exec_env_t exec_env,
    uint32_t offset,
    uint32_t length
) {
    const uint8_t *text;
    uint32_t index;

    if (length > SYSCALL_TRANSFER_MAX) {
        return JANI_EINVAL;
    }

    text = linear_memory(exec_env, offset, length);
    if (text == NULL) {
        return JANI_ERANGE;
    }

    for (index = 0; index < length; index++) {
        printk("%c", (char)text[index]);
    }

    return (int32_t)length;
}

static int32_t jani_object_create_impl(
    wasm_exec_env_t exec_env,
    int64_t type_high,
    int64_t type_low,
    uint32_t size
) {
    struct object_id id;
    uint8_t *zeros;
    uint32_t slot;
    int stored;

    (void)exec_env;

    if ((current == NULL) || (size > SYSCALL_TRANSFER_MAX) ||
        current->root_id.low > UINT32_MAX || current->next_object_sequence == 0 ||
        current->next_object_sequence > UINT32_MAX) {
        return JANI_EINVAL;
    }

    id = component_make_id(
        COMPONENT_DATA_ID_HIGH,
        (current->root_id.low << 32) | current->next_object_sequence
    );
    struct capability_table previous = current->capability_table;
    uint32_t previous_count = current->capability_count;
    uint32_t previous_dirty = current->capabilities_dirty;
    if (!component_capability_insert(current, id, CAP_RIGHT_READ | CAP_RIGHT_WRITE |
                                     CAP_RIGHT_GRANT, 0, &slot)) return JANI_ENOSPC;

    zeros = kmalloc((size_t)size + 1u);
    if (zeros == NULL) {
        current->capability_table = previous;
        current->capability_count = previous_count;
        current->capabilities_dirty = previous_dirty;
        return JANI_ENOSPC;
    }
    memset(zeros, 0, (size_t)size + 1u);

    stored = current_write(
        id,
        component_make_id((uint64_t)type_high, (uint64_t)type_low),
        current->root_id,
        zeros, (size_t)size + ((size == 0) ? 1u : 0u)
    );
    kfree(zeros);

    if (!stored) {
        current->capability_table = previous;
        current->capability_count = previous_count;
        current->capabilities_dirty = previous_dirty;
        return JANI_ENOSPC;
    }

    current->next_object_sequence += 1;
    return (int32_t)slot;
}

static int32_t jani_object_read_impl(
    wasm_exec_env_t exec_env,
    int32_t slot,
    uint32_t offset,
    uint32_t pointer,
    uint32_t length
) {
    struct object_header header;
    struct object_id id;
    const uint8_t *payload;
    uint8_t *destination;
    size_t payload_size;
    uint32_t clamped;

    if (length > SYSCALL_TRANSFER_MAX) {
        return JANI_EINVAL;
    }
    if (!slot_object(slot, &id)) {
        return JANI_EINVAL;
    }
    if (!slot_allows(slot, COMPONENT_RIGHTS_READ)) {
        return JANI_EPERM;
    }

    if (!current_read(id, &header, &payload,
                          &payload_size)) {
        return JANI_ENOENT;
    }

    if (!jani_syscall_clamp_read(payload_size, offset, length,
                                 &clamped)) {
        return JANI_ERANGE;
    }

    destination = linear_memory(exec_env, pointer, clamped);
    if (destination == NULL) {
        return JANI_ERANGE;
    }

    if (clamped != 0) {
        memcpy(destination, payload + offset, clamped);
    }

    return (int32_t)clamped;
}

static int32_t jani_object_write_impl(
    wasm_exec_env_t exec_env,
    int32_t slot,
    uint32_t offset,
    uint32_t pointer,
    uint32_t length
) {
    struct object_header header;
    struct object_id id;
    const uint8_t *payload;
    const uint8_t *source;
    uint8_t *updated;
    size_t payload_size;
    int stored;

    if (!slot_object(slot, &id)) {
        return JANI_EINVAL;
    }
    if (!slot_allows(slot, COMPONENT_RIGHTS_WRITE)) {
        return JANI_EPERM;
    }
    if (length > SYSCALL_TRANSFER_MAX) {
        return JANI_EINVAL;
    }

    if (!current_read(id, &header, &payload,
                          &payload_size)) {
        return JANI_ENOENT;
    }

    if (!jani_syscall_check_span(payload_size, offset, length)) {
        return JANI_ERANGE;
    }

    source = linear_memory(exec_env, pointer, length);
    if (source == NULL) {
        return JANI_ERANGE;
    }

    updated = kmalloc(payload_size);
    if (updated == NULL) {
        return JANI_ENOSPC;
    }

    memcpy(updated, payload, payload_size);
    if (length != 0) {
        memcpy(updated + offset, source, length);
    }

    stored = current_write(id, header.type_id, header.creator_id, updated, payload_size);
    kfree(updated);

    if (!stored) {
        return JANI_ENOSPC;
    }

    return (int32_t)length;
}

static int64_t jani_object_size_impl(wasm_exec_env_t exec_env, int32_t slot) {
    struct object_header header;
    struct object_id id;
    const uint8_t *payload;
    size_t payload_size;

    (void)exec_env;

    if (!slot_object(slot, &id)) {
        return JANI_EINVAL;
    }
    if (!slot_allows(slot, COMPONENT_RIGHTS_READ)) {
        return JANI_EPERM;
    }

    if (!current_read(id, &header, &payload,
                          &payload_size)) {
        return JANI_ENOENT;
    }

    return (int64_t)payload_size;
}

static int32_t jani_cap_drop_impl(wasm_exec_env_t exec_env, int32_t slot) {
    struct object_id id;

    (void)exec_env;

    if (!slot_object(slot, &id)) {
        return JANI_EINVAL;
    }

    if (current->domain != NULL) return capability_domain_revoke(current->domain, current, (uint32_t)slot);
    if (!component_capability_revoke(current, (uint32_t)slot)) {
        return JANI_EINVAL;
    }

    return 0;
}

static int32_t jani_cap_derive_impl(
    wasm_exec_env_t exec_env,
    int32_t parent_slot,
    uint32_t child_rights,
    uint32_t child_badge
) {
    const struct capability *parent;
    uint32_t child_slot;

    (void)exec_env;

    if ((current == NULL) ||
        !jani_syscall_check_slot(current->capability_count, parent_slot)) {
        return JANI_EINVAL;
    }

    parent = capability_table_get(
        &current->capability_table,
        (uint32_t)parent_slot
    );
    if (parent == NULL) {
        return JANI_EINVAL;
    }

    if (!capability_allows(parent, CAP_RIGHT_GRANT) ||
        (child_rights == 0) ||
        ((child_rights & ~CAP_RIGHT_ALL) != 0) ||
        ((child_rights & parent->rights) != child_rights)) {
        return JANI_EPERM;
    }

    if (current->domain != NULL) {
        if (capability_domain_use(current->domain, current, parent_slot, CAP_RIGHT_GRANT,
                                  PROVENANCE_DERIVE) != 0) return JANI_EPERM;
        return capability_domain_derive(current->domain, current, (uint32_t)parent_slot,
                                         child_rights, child_badge);
    }

    if (!component_capability_derive(
            current,
            (uint32_t)parent_slot,
            child_rights,
            child_badge,
            &child_slot)) {
        return JANI_ENOSPC;
    }

    return (int32_t)child_slot;
}

static int32_t jani_cap_revoke_impl(
    wasm_exec_env_t exec_env,
    int32_t slot
) {
    const struct capability *capability;

    (void)exec_env;

    if ((current == NULL) ||
        !jani_syscall_check_slot(current->capability_count, slot)) {
        return JANI_EINVAL;
    }

    capability = capability_table_get(
        &current->capability_table,
        (uint32_t)slot
    );
    if (capability == NULL) {
        return JANI_EINVAL;
    }

    if (!capability_allows(capability, CAP_RIGHT_GRANT)) {
        return JANI_EPERM;
    }

    if (current->domain != NULL) {
        if (capability_domain_use(current->domain, current, slot, CAP_RIGHT_GRANT,
                                  PROVENANCE_REVOKE) != 0) return JANI_EPERM;
        return capability_domain_revoke(current->domain, current, (uint32_t)slot);
    }

    if (!component_capability_revoke(current, (uint32_t)slot)) {
        return JANI_EINVAL;
    }

    return 0;
}

static int32_t jani_message_send_impl(
    wasm_exec_env_t exec_env,
    int32_t target,
    uint32_t pointer,
    uint32_t length,
    int32_t capability,
    uint32_t rights,
    uint32_t badge
) {
    struct component *recipient;
    struct object_id id;
    const uint8_t *source;

    if (!slot_object(target, &id)) {
        return JANI_EINVAL;
    }
    if (!slot_allows(target, COMPONENT_RIGHTS_SEND)) {
        return JANI_EPERM;
    }
    if (length > SYSCALL_TRANSFER_MAX) {
        return JANI_EINVAL;
    }
    source = linear_memory(exec_env, pointer, length);
    if (source == NULL) return JANI_ERANGE;
    if (current->domain != NULL) {
        int result = capability_domain_send(current->domain, current, (uint32_t)target,
                                       source, length, capability, rights, badge);
        if (result == 0 && current->metrics.messages_sent != UINT64_MAX) current->metrics.messages_sent++;
        return result;
    }
    /* Bootstrap handlers can send payloads to themselves before registration. */
    if (capability != -1 || rights != 0 || badge != 0) return JANI_EPERM;
    if (!jani_syscall_check_optional_slot(current->capability_count,
                                          capability)) {
        return JANI_EINVAL;
    }
    if ((capability >= 0) &&
        (capability_table_get(&current->capability_table,
                              (uint32_t)capability) == NULL)) {
        return JANI_EINVAL;
    }

    if (object_id_equal(id, current->root_id)) {
        recipient = current;
    } else {
        recipient = component_set_find(running_components, id);
        if (recipient == NULL) {
            return JANI_ENOENT;
        }

        if (capability >= 0) {
            return JANI_EPERM;
        }
    }

    source = linear_memory(exec_env, pointer, length);
    if (source == NULL) {
        return JANI_ERANGE;
    }

    if (!component_mailbox_push(recipient, source, length, capability)) {
        return JANI_ENOSPC;
    }

    return 0;
}

static int32_t jani_message_recv_impl(
    wasm_exec_env_t exec_env,
    uint32_t pointer,
    uint32_t length,
    uint32_t capability_out
) {
    uint8_t staging[SYSCALL_TRANSFER_MAX];
    uint8_t *destination;
    uint8_t *capability_slot;
    uint32_t received;
    int32_t capability;

    if (current == NULL) {
        return JANI_EINVAL;
    }
    if (length > SYSCALL_TRANSFER_MAX) {
        return JANI_EINVAL;
    }

    destination = linear_memory(exec_env, pointer, length);
    if (destination == NULL) {
        return JANI_ERANGE;
    }

    capability_slot = NULL;
    if (capability_out != 0) {
        capability_slot = linear_memory(exec_env, capability_out,
                                        (uint32_t)sizeof(int32_t));
        if (capability_slot == NULL) {
            return JANI_ERANGE;
        }
    }

    if (!component_mailbox_pop(current, staging, length, &received,
                              &capability)) {
        return JANI_EAGAIN;
    }

    if (received != 0) {
        memcpy(destination, staging, received);
    }
    if (capability_slot != NULL) {
        memcpy(capability_slot, &capability, sizeof(capability));
    }

    return (int32_t)received;
}

static int32_t jani_timer_set_impl(
    wasm_exec_env_t exec_env,
    uint64_t delay_ticks
) {
    uint64_t deadline;

    (void)exec_env;

    if (current == NULL) {
        return JANI_EINVAL;
    }

    if (!jani_syscall_deadline(current->logical_time, delay_ticks,
                               &deadline)) {
        return JANI_EINVAL;
    }

    current->timer_deadline = deadline;
    current->timer_armed = 1;
    return 0;
}

static int32_t jani_provenance_impl(wasm_exec_env_t exec_env, int32_t slot,
                                   uint32_t age, uint32_t pointer, uint32_t length) {
    struct provenance_event event;
    struct object_id object;
    uint8_t *destination;
    if (current == NULL || current->domain == NULL || length != sizeof(event) ||
        !slot_object(slot, &object)) return JANI_EINVAL;
    /* Querying the ledger must not evict the history being inspected. */
    if (!capability_allows(capability_table_get(&current->capability_table, (uint32_t)slot),
                           CAP_RIGHT_READ)) return JANI_EPERM;
    destination = linear_memory(exec_env, pointer, length);
    if (destination == NULL) return JANI_ERANGE;
    if (!provenance_query(&current->domain->provenance, object, age, &event)) return JANI_ENOENT;
    memcpy(destination, &event, sizeof(event));
    return sizeof(event);
}

static int32_t jani_stats_impl(wasm_exec_env_t env, uint32_t pointer, uint32_t length) {
    if (current == NULL || length != sizeof(current->metrics)) return JANI_EINVAL;
    uint8_t *destination = linear_memory(env, pointer, length);
    if (destination == NULL) return JANI_ERANGE;
    memcpy(destination, &current->metrics, length);
    return (int32_t)length;
}

static int32_t jani_trace_impl(wasm_exec_env_t env, uint32_t age, uint32_t pointer, uint32_t length) {
    struct trace_event event;
    if (current == NULL || length != sizeof(event)) return JANI_EINVAL;
    uint8_t *destination = linear_memory(env, pointer, length);
    if (destination == NULL) return JANI_ERANGE;
    if (!scheduler_trace_query(scheduler_current(), current->root_id, age, &event)) return JANI_ENOENT;
    memcpy(destination, &event, length);
    return (int32_t)length;
}

static int64_t jani_time_logical_impl(wasm_exec_env_t exec_env) {
    (void)exec_env;

    if (current == NULL) {
        return JANI_EINVAL;
    }

    return (int64_t)current->logical_time;
}

static int32_t jani_self_impl(wasm_exec_env_t exec_env) {
    uint32_t slot;

    (void)exec_env;

    if (current == NULL) {
        return JANI_EINVAL;
    }

    if (!component_capability_find(current, current->root_id, &slot)) {
        return JANI_ENOENT;
    }

    return (int32_t)slot;
}

static void jani_exit_impl(wasm_exec_env_t exec_env, int32_t code) {
    if (current == NULL) {
        return;
    }

    current->exited = 1;
    current->exit_code = code;
    current->timer_armed = 0;

    wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env),
                               "jani_exit");
}


/* Capture bounded guest spans before the syscall can overwrite aliased input. */
struct syscall_capture {
    struct replay_session *session;
    uint8_t input[SYSCALL_TRANSFER_MAX];
    uint8_t output[SYSCALL_TRANSFER_MAX + 4];
    uint8_t *destination;
    uint8_t *auxiliary;
    size_t input_size;
    size_t output_size;
    size_t data_size;
    uint64_t arguments[8];
    int64_t result;
    uint32_t kind;
};

static int capture_before(struct syscall_capture *capture, wasm_exec_env_t env,
                           uint32_t kind, const uint64_t args[8],
                           uint32_t input, uint32_t input_length,
                           uint32_t output, uint32_t output_length, uint32_t auxiliary) {
    struct replay_session *session = replay_current();
    memset(capture, 0, sizeof(*capture));
    if (session == NULL || session->owner != current) return 1;
    capture->session = session;
    capture->kind = kind;
    memcpy(capture->arguments, args, sizeof(capture->arguments));
    if (input_length && input_length <= SYSCALL_TRANSFER_MAX) {
        uint8_t *source = linear_memory(env, input, input_length);
        if (source != NULL) {
            memcpy(capture->input, source, input_length);
            capture->input_size = input_length;
        }
    }
    if (output_length && output_length <= SYSCALL_TRANSFER_MAX) {
        uint8_t *base;
        size_t size;
        if (jani_wasm_instance_memory(wasm_runtime_get_module_inst(env), &base, &size) && output <= size) {
            size_t available = size - output;
            capture->data_size = output_length < available ? output_length : available;
            capture->destination = base + output;
        }
    }
    if (auxiliary != 0) capture->auxiliary = linear_memory(env, auxiliary, 4);
    capture->output_size = capture->data_size + (capture->auxiliary != NULL ? 4 : 0);
    int decision = replay_before(session, kind, args, capture->input, capture->input_size,
                                  capture->output, capture->output_size, &capture->result);
    if (decision == 0) {
        if (capture->data_size) memcpy(capture->destination, capture->output, capture->data_size);
        if (capture->auxiliary != NULL) memcpy(capture->auxiliary, capture->output + capture->data_size, 4);
    }
    if (decision < 0) {
        capture->result = JANI_EINVAL;
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), "replay input mismatch or capacity");
    }
    return decision;
}

static void capture_after(struct syscall_capture *capture, wasm_exec_env_t env) {
    if (capture->session == NULL) return;
    if (capture->data_size) memcpy(capture->output, capture->destination, capture->data_size);
    if (capture->auxiliary != NULL) memcpy(capture->output + capture->data_size, capture->auxiliary, 4);
    if (!replay_after(capture->session, capture->kind, capture->arguments,
        capture->input, capture->input_size, capture->output, capture->output_size, capture->result)) {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), "replay recorder overflow");
    }
}

#undef jani_log_impl
static int32_t jani_log_impl(wasm_exec_env_t exec_env, uint32_t offset, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {offset,length};
    int decision = capture_before(&capture, exec_env, 1, args, offset, length, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_log_live(exec_env,offset,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_object_create_impl
static int32_t jani_object_create_impl(wasm_exec_env_t exec_env, int64_t type_high, int64_t type_low, uint32_t size) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)type_high,(uint64_t)type_low,size};
    int decision = capture_before(&capture, exec_env, 2, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_object_create_live(exec_env,type_high,type_low,size);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_object_read_impl
static int32_t jani_object_read_impl(wasm_exec_env_t exec_env, int32_t slot, uint32_t offset, uint32_t pointer, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot,offset,pointer,length};
    int decision = capture_before(&capture, exec_env, 3, args, 0, 0, pointer, length, 0);
    if (decision == 1) {
        capture.result = jani_object_read_live(exec_env,slot,offset,pointer,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_object_write_impl
static int32_t jani_object_write_impl(wasm_exec_env_t exec_env, int32_t slot, uint32_t offset, uint32_t pointer, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot,offset,pointer,length};
    int decision = capture_before(&capture, exec_env, 4, args, pointer, length, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_object_write_live(exec_env,slot,offset,pointer,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_object_size_impl
static int64_t jani_object_size_impl(wasm_exec_env_t exec_env, int32_t slot) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot};
    int decision = capture_before(&capture, exec_env, 5, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_object_size_live(exec_env,slot);
        capture_after(&capture, exec_env);
    }
    return (int64_t)capture.result;
}

#undef jani_cap_drop_impl
static int32_t jani_cap_drop_impl(wasm_exec_env_t exec_env, int32_t slot) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot};
    int decision = capture_before(&capture, exec_env, 6, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_cap_drop_live(exec_env,slot);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_cap_derive_impl
static int32_t jani_cap_derive_impl(wasm_exec_env_t exec_env, int32_t parent_slot, uint32_t child_rights, uint32_t child_badge) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)parent_slot,child_rights,child_badge};
    int decision = capture_before(&capture, exec_env, 7, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_cap_derive_live(exec_env,parent_slot,child_rights,child_badge);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_cap_revoke_impl
static int32_t jani_cap_revoke_impl(wasm_exec_env_t exec_env, int32_t slot) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot};
    int decision = capture_before(&capture, exec_env, 8, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_cap_revoke_live(exec_env,slot);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_message_send_impl
static int32_t jani_message_send_cap_impl(wasm_exec_env_t exec_env, int32_t target, uint32_t pointer, uint32_t length, int32_t capability, uint32_t rights, uint32_t badge) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)target,pointer,length,(uint64_t)capability,rights,badge};
    int decision = capture_before(&capture, exec_env, 9, args, pointer, length, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_message_send_live(exec_env,target,pointer,length,capability,rights,badge);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_message_recv_impl
/* Preserve the Phase 3 import signature for already installed modules. */
static int32_t jani_message_send_impl(wasm_exec_env_t env, int32_t target, uint32_t pointer, uint32_t length, int32_t capability) {
    return jani_message_send_cap_impl(env, target, pointer, length, capability, 0, 0);
}

static int32_t jani_message_recv_impl(wasm_exec_env_t exec_env, uint32_t pointer, uint32_t length, uint32_t capability_out) {
    struct syscall_capture capture;
    const uint64_t args[8] = {pointer,length,capability_out};
    int decision = capture_before(&capture, exec_env, 10, args, 0, 0, pointer, length, capability_out);
    if (decision == 1) {
        capture.result = jani_message_recv_live(exec_env,pointer,length,capability_out);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_timer_set_impl
static int32_t jani_timer_set_impl(wasm_exec_env_t exec_env, uint64_t delay_ticks) {
    struct syscall_capture capture;
    const uint64_t args[8] = {delay_ticks};
    int decision = capture_before(&capture, exec_env, 11, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_timer_set_live(exec_env,delay_ticks);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_time_logical_impl
static int64_t jani_time_logical_impl(wasm_exec_env_t exec_env) {
    struct syscall_capture capture;
    const uint64_t args[8] = {0};
    int decision = capture_before(&capture, exec_env, 12, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_time_logical_live(exec_env);
        capture_after(&capture, exec_env);
    }
    return (int64_t)capture.result;
}

#undef jani_self_impl
static int32_t jani_self_impl(wasm_exec_env_t exec_env) {
    struct syscall_capture capture;
    const uint64_t args[8] = {0};
    int decision = capture_before(&capture, exec_env, 13, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = jani_self_live(exec_env);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_exit_impl
static void jani_exit_impl(wasm_exec_env_t exec_env, int32_t code) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)code};
    int decision = capture_before(&capture, exec_env, 14, args, 0, 0, 0, 0, 0);
    if (decision == 1) {
        capture.result = (jani_exit_live(exec_env,code), 0);
        capture_after(&capture, exec_env);
    }
    return (void)capture.result;
}

#undef jani_provenance_impl
static int32_t jani_provenance_impl(wasm_exec_env_t exec_env, int32_t slot, uint32_t age, uint32_t pointer, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {(uint64_t)slot,age,pointer,length};
    int decision = capture_before(&capture, exec_env, 15, args, 0, 0, pointer, length, 0);
    if (decision == 1) {
        capture.result = jani_provenance_live(exec_env,slot,age,pointer,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_stats_impl
static int32_t jani_stats_impl(wasm_exec_env_t exec_env, uint32_t pointer, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {pointer,length};
    int decision = capture_before(&capture, exec_env, 16, args, 0, 0, pointer, length, 0);
    if (decision == 1) {
        capture.result = jani_stats_live(exec_env,pointer,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

#undef jani_trace_impl
static int32_t jani_trace_impl(wasm_exec_env_t exec_env, uint32_t age, uint32_t pointer, uint32_t length) {
    struct syscall_capture capture;
    const uint64_t args[8] = {age,pointer,length};
    int decision = capture_before(&capture, exec_env, 17, args, 0, 0, pointer, length, 0);
    if (decision == 1) {
        capture.result = jani_trace_live(exec_env,age,pointer,length);
        capture_after(&capture, exec_env);
    }
    return (int32_t)capture.result;
}

static int32_t jani_driver_request_impl(wasm_exec_env_t env, uint32_t pointer, uint32_t length) {
    if (length != 528) return JANI_EINVAL;
    uint8_t *bytes = linear_memory(env, pointer, length);
    return bytes == NULL ? JANI_ERANGE : block_component_request(current, bytes, length);
}
static int32_t jani_driver_complete_impl(wasm_exec_env_t env, int32_t status, uint32_t pointer, uint32_t length) {
    if (length > 512) return JANI_EINVAL;
    uint8_t *bytes = linear_memory(env, pointer, length);
    return bytes == NULL ? JANI_ERANGE : block_component_complete(current, status, bytes, length);
}
static int32_t jani_dma_read_impl(wasm_exec_env_t env, int32_t slot, uint32_t offset, uint32_t pointer, uint32_t length) {
    if (length > 4096) return JANI_EINVAL;
    uint8_t *bytes = linear_memory(env, pointer, length);
    return bytes == NULL ? JANI_ERANGE : block_component_dma(current, slot, offset, bytes, length, 0);
}
static int32_t jani_dma_write_impl(wasm_exec_env_t env, int32_t slot, uint32_t offset, uint32_t pointer, uint32_t length) {
    if (length > 4096) return JANI_EINVAL;
    uint8_t *bytes = linear_memory(env, pointer, length);
    return bytes == NULL ? JANI_ERANGE : block_component_dma(current, slot, offset, bytes, length, 1);
}
static int32_t jani_queue_submit_impl(wasm_exec_env_t env, int32_t slot, uint32_t pointer, uint32_t length) {
    if (length > 36) return JANI_EINVAL;
    uint8_t *bytes = linear_memory(env, pointer, length);
    return bytes == NULL ? JANI_ERANGE : block_component_submit(current, slot, bytes, length);
}
static int32_t jani_driver_restart_impl(wasm_exec_env_t env) {
    (void)env;
    return block_component_restart(current);
}

#include "generated/syscall_table.h"

void *jani_syscall_symbols(uint32_t *count_out) {
    if (count_out != NULL) {
        *count_out = (uint32_t)(sizeof(jani_symbols) / sizeof(NativeSymbol));
    }

    return jani_symbols;
}
