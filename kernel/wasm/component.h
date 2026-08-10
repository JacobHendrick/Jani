#ifndef JANI_KERNEL_WASM_COMPONENT_H
#define JANI_KERNEL_WASM_COMPONENT_H

#include <stddef.h>
#include <stdint.h>

#include "../obj/object_id.h"
#include "../obj/object_store.h"

#define COMPONENT_REGISTRY_ID_HIGH UINT64_C(1)
#define COMPONENT_REGISTRY_ID_LOW UINT64_C(0)
#define COMPONENT_SEQUENCE_ID_HIGH UINT64_C(2)

#define COMPONENT_TYPE_REGISTRY UINT64_C(1)
#define COMPONENT_TYPE_ROOT UINT64_C(2)
#define COMPONENT_TYPE_MODULE UINT64_C(3)
#define COMPONENT_TYPE_CAPTABLE UINT64_C(4)
#define COMPONENT_TYPE_INSTANCE_STATE UINT64_C(5)
#define COMPONENT_TYPE_DATA UINT64_C(6)

#define COMPONENT_DATA_ID_HIGH UINT64_C(3)

#define COMPONENT_CAPTABLE_MAGIC UINT64_C(0x4A414E495F434150)
#define COMPONENT_CAPTABLE_FORMAT_VERSION_V1 UINT32_C(1)
#define COMPONENT_CAPTABLE_FORMAT_VERSION UINT32_C(2)
#define COMPONENT_CAPTABLE_HEADER_SIZE 32u

#define JANI_EINVAL (-1)
#define JANI_EPERM (-2)
#define JANI_ENOSPC (-3)
#define JANI_EAGAIN (-4)
#define JANI_ERANGE (-5)
#define JANI_ENOENT (-6)

#define COMPONENT_MAX 4u
#define COMPONENT_CAP_SLOTS 16u
#define COMPONENT_CAP_PARENT_NONE UINT32_MAX
#define COMPONENT_MAILBOX_BYTES 512u

#define COMPONENT_RIGHTS_READ UINT32_C(0x1)
#define COMPONENT_RIGHTS_WRITE UINT32_C(0x2)
#define COMPONENT_RIGHTS_SEND UINT32_C(0x4)
#define COMPONENT_RIGHTS_GRANT UINT32_C(0x8)

#define COMPONENT_REGISTRY_MAGIC UINT64_C(0x4A414E495F524547)
#define COMPONENT_REGISTRY_FORMAT_VERSION UINT32_C(1)
#define COMPONENT_REGISTRY_HEADER_SIZE 32u

#define COMPONENT_ROOT_MAGIC UINT64_C(0x4A414E495F524F54)
#define COMPONENT_ROOT_FORMAT_VERSION UINT32_C(1)
#define COMPONENT_ROOT_SIZE 72u

struct component_registry_header {
    uint64_t magic;
    uint32_t format_version;
    uint32_t component_count;
    uint64_t next_sequence;
    uint32_t payload_crc32c;
    uint32_t _reserved;
};

_Static_assert(
    sizeof(struct component_registry_header) == COMPONENT_REGISTRY_HEADER_SIZE,
    "registry header must be exactly 32 bytes"
);

struct component_root_record {
    uint64_t magic;
    uint32_t format_version;
    uint32_t _reserved;

    struct object_id module_id;
    struct object_id captable_id;
    struct object_id state_id;

    uint32_t payload_crc32c;
    uint32_t _padding;
};

_Static_assert(
    sizeof(struct component_root_record) == COMPONENT_ROOT_SIZE,
    "component root record must be exactly 72 bytes"
);

struct component_captable_header {
    uint64_t magic;
    uint32_t format_version;
    uint32_t slot_count;
    uint64_t next_object_sequence;
    uint32_t payload_crc32c;
    uint32_t _reserved;
};

_Static_assert(
    sizeof(struct component_captable_header) == COMPONENT_CAPTABLE_HEADER_SIZE,
    "capability table header must be exactly 32 bytes"
);

struct component_capability {
    struct object_id object;
    uint32_t rights;
    uint32_t badge;
};

struct component {
    struct object_id root_id;
    struct object_id module_id;
    struct object_id captable_id;
    struct object_id state_id;

    uint64_t logical_time;
    uint64_t timer_deadline;
    uint32_t timer_armed;

    uint32_t mailbox_used;
    uint8_t mailbox[COMPONENT_MAILBOX_BYTES];

    struct component_capability capabilities[COMPONENT_CAP_SLOTS];
    uint32_t capability_parents[COMPONENT_CAP_SLOTS];
    uint32_t capability_count;
    uint64_t next_object_sequence;
    uint32_t capabilities_dirty;
    uint32_t exited;
    int32_t exit_code;

    struct object_store *store;
    void *module;
    void *instance;
    void *exec_env;
    void *module_bytes;
};

struct object_id component_make_id(uint64_t high, uint64_t low);

int component_capability_find(
    const struct component *component,
    struct object_id object,
    uint32_t *slot_out
);

int component_capability_insert(
    struct component *component,
    struct object_id object,
    uint32_t rights,
    uint32_t badge,
    uint32_t *slot_out
);

int component_captable_write(
    struct object_store *store,
    struct component *component
);

int component_captable_read(
    struct object_store *store,
    struct component *component
);

int component_mailbox_push(
    struct component *component,
    const uint8_t *bytes,
    uint32_t length,
    int32_t capability_slot
);

int component_mailbox_pop(
    struct component *component,
    uint8_t *bytes_out,
    uint32_t capacity,
    uint32_t *length_out,
    int32_t *capability_slot_out
);

int component_registry_load(
    struct object_store *store,
    struct object_id *roots_out,
    size_t capacity,
    size_t *count_out,
    uint64_t *next_sequence_out
);

int component_registry_store(
    struct object_store *store,
    const struct object_id *roots,
    size_t count,
    uint64_t next_sequence
);

int component_install(
    struct object_store *store,
    const uint8_t *module_bytes,
    size_t module_size,
    struct component *component_out
);

int component_resume(
    struct object_store *store,
    struct object_id root_id,
    struct component *component_out
);

int component_uninstall(
    struct object_store *store,
    struct object_id root_id
);

int component_commit(
    struct object_store *store,
    struct component *component
);

int component_invoke_timer(struct component *component);

int component_release(struct component *component);

#endif
