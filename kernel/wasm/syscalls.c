#include "syscalls.h"

#include "wasm_export.h"

#include "component.h"
#include "syscall_args.h"
#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../obj/object_header.h"

#define SYSCALL_TRANSFER_MAX 4096u

static struct component *current;

void jani_syscall_set_current(struct component *component) {
    current = component;
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
    if (current == NULL) {
        return 0;
    }

    if (!jani_syscall_check_slot(current->capability_count, slot)) {
        return 0;
    }

    if (object_id_is_zero(current->capabilities[slot].object)) {
        return 0;
    }

    *object_out = current->capabilities[slot].object;
    return 1;
}

static int slot_allows(int32_t slot, uint32_t rights) {
    return (current->capabilities[slot].rights & rights) == rights;
}

static int32_t jani_log_impl(
    wasm_exec_env_t exec_env,
    uint32_t offset,
    uint32_t length
) {
    const uint8_t *text;
    uint32_t index;

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

    if ((current == NULL) || (size > SYSCALL_TRANSFER_MAX)) {
        return JANI_EINVAL;
    }

    id = component_make_id(
        COMPONENT_DATA_ID_HIGH,
        (current->root_id.low << 32) | current->next_object_sequence
    );

    zeros = kmalloc((size_t)size + 1u);
    if (zeros == NULL) {
        return JANI_ENOSPC;
    }
    memset(zeros, 0, (size_t)size + 1u);

    stored = object_store_put(
        current->store, id,
        component_make_id((uint64_t)type_high, (uint64_t)type_low),
        current->root_id, current->root_id, current->logical_time,
        zeros, (size_t)size + ((size == 0) ? 1u : 0u)
    );
    kfree(zeros);

    if (!stored) {
        return JANI_ENOSPC;
    }

    if (!component_capability_insert(current, id,
                                     COMPONENT_RIGHTS_READ |
                                     COMPONENT_RIGHTS_WRITE |
                                     COMPONENT_RIGHTS_GRANT,
                                     0, &slot)) {
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

    if (!slot_object(slot, &id)) {
        return JANI_EINVAL;
    }
    if (!slot_allows(slot, COMPONENT_RIGHTS_READ)) {
        return JANI_EPERM;
    }

    if (!object_store_get(current->store, id, &header, &payload,
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

    if (!object_store_get(current->store, id, &header, &payload,
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

    stored = object_store_put(current->store, id, header.type_id,
                              current->root_id, current->root_id,
                              current->logical_time, updated, payload_size);
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

    if (!object_store_get(current->store, id, &header, &payload,
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

    current->capabilities[slot].object = component_make_id(0, 0);
    current->capabilities[slot].rights = 0;
    current->capabilities[slot].badge = 0;
    current->capabilities_dirty = 1;
    return 0;
}

static int32_t jani_message_send_impl(
    wasm_exec_env_t exec_env,
    int32_t target,
    uint32_t pointer,
    uint32_t length,
    int32_t capability
) {
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
    if (!jani_syscall_check_optional_slot(current->capability_count,
                                          capability)) {
        return JANI_EINVAL;
    }

    if (!object_id_equal(id, current->root_id)) {
        return JANI_ENOENT;
    }

    source = linear_memory(exec_env, pointer, length);
    if (source == NULL) {
        return JANI_ERANGE;
    }

    if (!component_mailbox_push(current, source, length, capability)) {
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

    if (!component_mailbox_pop(current, staging, length, &received,
                              &capability)) {
        return JANI_EAGAIN;
    }

    destination = linear_memory(exec_env, pointer, received);
    if (destination == NULL) {
        return JANI_ERANGE;
    }

    if (received != 0) {
        memcpy(destination, staging, received);
    }

    if (capability_out != 0) {
        capability_slot = linear_memory(exec_env, capability_out,
                                        (uint32_t)sizeof(int32_t));
        if (capability_slot == NULL) {
            return JANI_ERANGE;
        }
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

#include "generated/syscall_table.h"

void *jani_syscall_symbols(uint32_t *count_out) {
    if (count_out != NULL) {
        *count_out = (uint32_t)(sizeof(jani_symbols) / sizeof(NativeSymbol));
    }

    return jani_symbols;
}
