#include "block_component.h"
#include "virtio_blk.h"
#include "../wasm/runtime.h"
#include "../wasm/syscalls.h"
#include "../lib/string.h"
#include "../lib/printk.h"

extern const uint8_t _binary_build_block_driver_wasm_start[], _binary_build_block_driver_wasm_end[];
extern const uint8_t _binary_build_block_supervisor_wasm_start[], _binary_build_block_supervisor_wasm_end[];
static struct component driver, supervisor;
static uint32_t generation;
static int alive, busy, submitted, completed;
static uint8_t request_bytes[528], response_bytes[512];
static uint32_t operation;
static uint64_t request_sector;
static int response_status;

static int instantiate(struct component *c, const uint8_t *bytes, size_t size, uint64_t identity) {
    memset(c, 0, sizeof(*c));
    c->root_id = (struct object_id){10, identity};
    capability_table_init(&c->capability_table);
    struct capability cap = {{10, identity}, CAP_RIGHT_READ | CAP_RIGHT_WRITE, 0};
    uint32_t slot;
    if (!capability_table_insert_root(&c->capability_table, &cap, &slot)) return 0;
    c->capability_count = 1;
    return jani_wasm_instance_create(bytes, size, &c->module, &c->instance, &c->exec_env, &c->module_bytes);
}

static int driver_authorized(struct component *caller, int32_t slot) {
    const struct capability *cap;
    if (caller != &driver || !alive || !busy || slot != 0) return 0;
    cap = capability_table_get(&driver.capability_table, 0);
    return cap != NULL && object_id_equal(cap->object, (struct object_id){10, 1}) &&
        capability_allows(cap, CAP_RIGHT_READ | CAP_RIGHT_WRITE) &&
        driver.capability_table.generations[0] == generation;
}

int block_component_restart(struct component *caller) {
    if (caller != &supervisor || alive || busy || generation == UINT32_MAX ||
        !capability_allows(capability_table_get(&supervisor.capability_table, 0), CAP_RIGHT_WRITE)) return JANI_EPERM;
    jani_wasm_instance_destroy(driver.module, driver.instance, driver.exec_env, driver.module_bytes);
    if (!instantiate(&driver, _binary_build_block_driver_wasm_start,
        (size_t)(_binary_build_block_driver_wasm_end - _binary_build_block_driver_wasm_start), 1)) return JANI_ENOSPC;
    driver.capability_table.generations[0] = ++generation;
    alive = 1;
    return 0;
}

static int ensure_running(void) {
    if (alive) return 1;
    struct component *previous = jani_syscall_current();
    jani_wasm_set_current_component(&supervisor);
    int ok = jani_wasm_instance_call(supervisor.instance, supervisor.exec_env, "jani_on_message");
    jani_wasm_set_current_component(previous);
    return ok && alive;
}

int block_component_start(void) {
    struct component *previous = jani_syscall_current();
    jani_wasm_set_current_component(NULL);
    int ok = jani_wasm_runtime_start() && instantiate(&supervisor,
        _binary_build_block_supervisor_wasm_start,
        (size_t)(_binary_build_block_supervisor_wasm_end - _binary_build_block_supervisor_wasm_start), 2) && ensure_running();
    jani_wasm_set_current_component(previous);
    if (ok) kputs("virtio-blk: Zig driver and supervisor ready\n");
    return ok;
}

int block_component_request(struct component *caller, uint8_t *bytes, uint32_t length) {
    if (!driver_authorized(caller, 0) || length != sizeof(request_bytes) || bytes == NULL) return JANI_EPERM;
    memcpy(bytes, request_bytes, sizeof(request_bytes));
    return sizeof(request_bytes);
}

int block_component_complete(struct component *caller, int32_t status, const uint8_t *bytes, uint32_t length) {
    if (!driver_authorized(caller, 0) || completed || !submitted ||
        (status != 0 && status != -1) ||
        (status == 0 && length != (operation == VIRTIO_BLK_T_IN ? 512u : 0u)) ||
        length > sizeof(response_bytes) || (length && bytes == NULL)) return JANI_EPERM;
    if (length) memcpy(response_bytes, bytes, length);
    response_status = status;
    completed = 1;
    return 0;
}

int block_component_dma(struct component *caller, int32_t slot, uint32_t offset,
                         uint8_t *bytes, uint32_t length, int write) {
    if (!driver_authorized(caller, slot) || (submitted && write)) return JANI_EPERM;
    return virtio_blk_dma(offset, bytes, length, write) ? (int)length : JANI_ERANGE;
}

int block_component_submit(struct component *caller, int32_t slot, const uint8_t *bytes, uint32_t length) {
    if (!driver_authorized(caller, slot) || submitted) return JANI_EPERM;
    submitted = 1;
    return virtio_blk_submit(bytes, length, operation, request_sector) ? 0 : JANI_EINVAL;
}

static int request(uint32_t type, uint64_t sector, const uint8_t *input, uint8_t *output) {
    if (busy || sector >= virtio_blk_capacity_sectors() || !ensure_running()) return 0;
    memset(request_bytes, 0, sizeof(request_bytes));
    memcpy(request_bytes, &type, 4);
    memcpy(request_bytes + 8, &sector, 8);
    if (input != NULL) memcpy(request_bytes + 16, input, 512);
    operation = type;
    request_sector = sector;
    submitted = completed = 0;
    response_status = -1;
    busy = 1;
    struct component *previous = jani_syscall_current();
    jani_wasm_set_current_component(&driver);
    int ok = jani_wasm_instance_call(driver.instance, driver.exec_env, "jani_on_message");
    jani_wasm_set_current_component(previous);
    busy = 0;
    if (!ok || !completed) alive = 0;
    if (!ok || !completed || response_status != 0) return 0;
    if (output != NULL) memcpy(output, response_bytes, 512);
    return 1;
}

int virtio_blk_io_read(void *context, uint64_t sector, uint8_t *buffer) {
    (void)context;
    return buffer != NULL && request(VIRTIO_BLK_T_IN, sector, NULL, buffer);
}
int virtio_blk_io_write(void *context, uint64_t sector, const uint8_t *buffer) {
    (void)context;
    return buffer != NULL && request(VIRTIO_BLK_T_OUT, sector, buffer, NULL);
}
int virtio_blk_io_flush(void *context) {
    (void)context;
    return request(VIRTIO_BLK_T_FLUSH, 0, NULL, NULL);
}

int block_component_crash_test(void) {
    uint8_t before[512], after[512];
    if (busy || !virtio_blk_io_read(NULL, 0, before)) return 0;
    struct component *previous = jani_syscall_current();
    jani_wasm_set_current_component(&driver);
    int trapped = !jani_wasm_instance_call(driver.instance, driver.exec_env, "jani_kill");
    jani_wasm_set_current_component(previous);
    alive = 0;
    if (!trapped || !virtio_blk_io_read(NULL, 0, after)) return 0;
    for (size_t i = 0; i < sizeof(before); i++) if (before[i] != after[i]) return 0;
    kputs("phase4: storage driver restarted; disk bytes unchanged\n");
    return 1;
}
