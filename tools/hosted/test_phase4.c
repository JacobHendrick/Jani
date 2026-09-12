#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/cap/domain.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/wasm/instance_state.h"
#include "../../kernel/sched/scheduler.h"
#include "../../kernel/wasm/service.h"
#include "../../kernel/replay/record.h"
#include "check.h"

unsigned long checks_passed;
static uint8_t disk[8192][512];
static uint8_t baseline_disk[sizeof(disk)];
static unsigned long writes_allowed = ULONG_MAX;
static unsigned long writes;
static struct object_store store;
static struct object_table_entry entries[64], scratch[64];
static uint8_t cache[65536], bitmap[1024], arena[65536];
static struct component components[3];
static uint8_t memories[3][256];
static struct component_set set;
static struct capability_domain domain;
static struct component *current;
static int (*handler)(struct component *component);

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *pointer) { free(pointer); }
int jani_wasm_instance_create(const uint8_t *b, size_t n, void **m, void **i, void **e, void **o) {
    if (b == NULL || n == 0 || b[0] == 0xff) return 0;
    *i = calloc(1, 256);
    if (*i == NULL) return 0;
    ((uint8_t *)*i)[255] = b[0];
    *m = *e = NULL;
    *o = *i;
    return 1;
}
void jani_wasm_instance_destroy(void *m, void *i, void *e, void *o) {
    (void)m; (void)i; (void)e;
    free(o);
}
int jani_wasm_instance_memory(void *instance, uint8_t **base, size_t *size) {
    if (instance == NULL) return 0;
    *base = instance; *size = 256; return 1;
}
int jani_wasm_instance_memory_grow(void *i, size_t n) { (void)i; return n <= 256; }
int jani_wasm_instance_has_handler(void *i, const char *name) { (void)name; return i != NULL && ((uint8_t *)i)[255] != 0xfe; }
int jani_wasm_instance_call(void *i, void *e, const char *n) { (void)i; (void)e; (void)n; return handler == NULL ? 1 : handler(current); }
void jani_wasm_set_current_component(struct component *c) { current = c; }

static int read_sector(void *context, uint64_t sector, uint8_t *buffer) {
    (void)context;
    if (sector >= 8192) return 0;
    memcpy(buffer, disk[sector], 512); return 1;
}
static int write_sector(void *context, uint64_t sector, const uint8_t *buffer) {
    (void)context;
    if (sector >= 8192 || writes_allowed == 0) return 0;
    writes_allowed--; writes++;
    memcpy(disk[sector], buffer, 512); return 1;
}
static int flush(void *context) { (void)context; return writes_allowed != 0; }

static void mount_store(int format) {
    struct object_store_io io = {disk, 8192, read_sector, write_sector, flush};
    if (format) CHECK(object_store_format(&store, io, entries, scratch, 64,
        cache, sizeof(cache), bitmap, sizeof(bitmap), arena, sizeof(arena)));
    else CHECK(object_store_mount(&store, io, entries, scratch, 64,
        cache, sizeof(cache), bitmap, sizeof(bitmap), arena, sizeof(arena)));
}

static void setup(void) {
    scheduler_set_current(NULL);
    handler = NULL;
    writes_allowed = ULONG_MAX;
    memset(disk, 0, sizeof(disk));
    memset(components, 0, sizeof(components));
    memset(memories, 0, sizeof(memories));
    mount_store(1);
    component_set_init(&set);
    for (uint32_t i = 0; i < 3; i++) {
        struct component *c = &components[i];
        c->root_id = (struct object_id){2, i * 4 + 4};
        c->module_id = (struct object_id){2, i * 4 + 1};
        c->captable_id = (struct object_id){2, i * 4 + 2};
        c->state_id = (struct object_id){2, i * 4 + 3};
        c->store = &store;
        c->instance = memories[i];
        capability_table_init(&c->capability_table);
        CHECK(component_set_add(&set, c));
        CHECK(component_commit(&store, c));
        CHECK(component_captable_write(&store, c));
        const uint8_t module[] = {0};
        CHECK(object_store_put(&store, c->module_id, (struct object_id){0, COMPONENT_TYPE_MODULE},
            c->root_id, c->root_id, 0, module, sizeof(module)));
        struct component_root_record root = {0};
        root.magic = COMPONENT_ROOT_MAGIC;
        root.format_version = COMPONENT_ROOT_FORMAT_VERSION;
        root.module_id = c->module_id;
        root.captable_id = c->captable_id;
        root.state_id = c->state_id;
        root.payload_crc32c = object_crc32c((const uint8_t *)&root.module_id, 48);
        CHECK(object_store_put(&store, c->root_id, (struct object_id){0, COMPONENT_TYPE_ROOT},
            c->root_id, c->root_id, 0, (const uint8_t *)&root, sizeof(root)));
    }
    CHECK(capability_domain_open(&domain, &store, &set));
    uint32_t slot;
    const uint8_t data[] = {7};
    CHECK(object_store_put(&store, (struct object_id){3, 1}, (struct object_id){0, COMPONENT_TYPE_DATA},
        components[0].root_id, components[0].root_id, 0, data, sizeof(data)));
    CHECK(component_capability_insert(&components[0], (struct object_id){3, 1}, CAP_RIGHT_ALL, 0, &slot));
    CHECK(slot == 0);
    CHECK(component_capability_insert(&components[0], components[1].root_id, CAP_RIGHT_SEND, 0, &slot));
    CHECK(component_capability_insert(&components[1], components[2].root_id, CAP_RIGHT_SEND, 0, &slot));
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
}

static void reload(void) {
    writes_allowed = ULONG_MAX;
    mount_store(0);
    for (uint32_t i = 0; i < 3; i++) {
        struct object_header h;
        const uint8_t *bytes;
        size_t length;
        CHECK(component_captable_read(&store, &components[i]));
        CHECK(object_store_get(&store, components[i].state_id, &h, &bytes, &length));
        CHECK(instance_state_deserialize(&components[i], memories[i], 256, bytes, length));
    }
    CHECK(capability_domain_open(&domain, &store, &set));
}

static void test_lineage(void) {
    struct capability_derivation_table graph = {0}, loaded = {0};
    uint8_t bytes[CAP_LINEAGE_BYTES + 1];
    struct capability_ref a = capability_ref_make((struct object_id){1, 1}, 0, 1);
    struct capability_ref b = capability_ref_make((struct object_id){1, 2}, 1, 1);
    CHECK(capability_derivation_add(&graph, a, b));
    CHECK(capability_lineage_encode(&graph, bytes + 1, CAP_LINEAGE_BYTES));
    CHECK(capability_lineage_validate(bytes + 1, CAP_LINEAGE_BYTES));
    CHECK(capability_lineage_decode(&loaded, bytes + 1, CAP_LINEAGE_BYTES));
    CHECK(loaded.count == 1);
    for (size_t n = 0; n < CAP_LINEAGE_BYTES; n++) CHECK(!capability_lineage_validate(bytes + 1, n));
    CHECK(!capability_lineage_validate(NULL, CAP_LINEAGE_BYTES));
    bytes[25] ^= 1;
    CHECK(!capability_lineage_decode(&loaded, bytes + 1, CAP_LINEAGE_BYTES));
    CHECK(loaded.count == 1);
    CHECK(capability_lineage_encode(&graph, bytes + 1, CAP_LINEAGE_BYTES));
    memset(bytes + 1 + 24 + 24 + 20, 0, 4);
    uint32_t crc = object_crc32c(bytes + 1 + 24, CAP_LINEAGE_BYTES - 24);
    memcpy(bytes + 1 + 16, &crc, 4);
    CHECK(!capability_lineage_validate(bytes + 1, CAP_LINEAGE_BYTES));
}

static void test_delivery(void) {
    setup();
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[0], 1, (const uint8_t *)"hi", 2, 0,
                                 CAP_RIGHT_READ | CAP_RIGHT_GRANT, 7) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    reload();
    CHECK(components[1].capability_count == 2);
    CHECK(components[1].capability_table.slots[1].badge == 7);
    CHECK(domain.lineage.count == 1);
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[1], 0, NULL, 0, 1, CAP_RIGHT_WRITE, 0) == JANI_EPERM);
    CHECK(components[2].mailbox_used == 0);
    CHECK(capability_domain_send(&domain, &components[1], 0, NULL, 0, 1, CAP_RIGHT_READ, 0) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    reload();
    CHECK(domain.lineage.count == 2);
    struct capability_ref stale = capability_ref_make(components[2].root_id, 0, 1);
    CHECK(capability_domain_resolve(&domain, stale) != NULL);
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_revoke(&domain, &components[0], 0) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    reload();
    CHECK(domain.lineage.count == 0);
    CHECK(capability_domain_resolve(&domain, stale) == NULL);
    uint32_t slot;
    CHECK(component_capability_insert(&components[2], (struct object_id){3, 2}, CAP_RIGHT_READ, 0, &slot));
    CHECK(slot == 0);
    CHECK(capability_domain_resolve(&domain, stale) == NULL);
}

static void test_delivery_crashes(void) {
    setup();
    memcpy(baseline_disk, disk, sizeof(disk));
    writes = 0;
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[0], 1, (const uint8_t *)"hello", 5, 0, CAP_RIGHT_READ, 0) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    unsigned long total = writes;
    for (unsigned long cut = 0; cut <= total; cut++) {
        memcpy(disk, baseline_disk, sizeof(disk));
        reload();
        CHECK(capability_domain_begin(&domain));
        CHECK(capability_domain_send(&domain, &components[0], 1, (const uint8_t *)"hello", 5, 0, CAP_RIGHT_READ, 0) == 0);
        writes_allowed = cut;
        enum object_store_batch_result result = capability_domain_commit(&domain);
        if (result == OBJECT_STORE_BATCH_RECOVERY_REQUIRED) {
            CHECK(!capability_domain_begin(&domain));
            CHECK(object_store_requires_recovery(&store));
        }
        reload();
        int delivered = components[1].mailbox_used != 0;
        CHECK((components[1].capability_count == 2) == delivered);
        CHECK((domain.lineage.count == 1) == delivered);
    }
}

static uint64_t clock_ticks;
static uint64_t test_clock(void) { clock_ticks += 10; return clock_ticks; }
static int successful_handler(struct component *c) {
    ((uint8_t *)c->instance)[0]++;
    c->timer_armed = 1;
    c->timer_deadline = c->logical_time + 1;
    return 1;
}
static int failing_handler(struct component *c) {
    ((uint8_t *)c->instance)[0] = 99;
    CHECK(capability_domain_send(c->domain, c, 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == 0);
    return 0;
}
static void test_scheduler(void) {
    struct scheduler scheduler;
    struct trace_event event;
    setup();
    CHECK(scheduler_init(&scheduler, &domain, 1, 1000, test_clock));
    CHECK(!scheduler_configure(&scheduler, 0, (struct scheduler_policy){1, 0, 1001}));
    CHECK(scheduler_configure(&scheduler, 0, (struct scheduler_policy){1, 0, 600}));
    CHECK(!scheduler_configure(&scheduler, 1, (struct scheduler_policy){1, 0, 500}));
    handler = successful_handler;
    for (uint32_t i = 0; i < 3; i++) { components[i].timer_armed = 1; components[i].timer_deadline = 1; }
    CHECK(scheduler_step(&scheduler, 1) == 1);
    CHECK(memories[0][0] == 1);
    CHECK(components[0].metrics.invocations == 1);
    CHECK(components[0].metrics.cycles == 10);
    CHECK(scheduler_trace_query(&scheduler, components[0].root_id, 0, &event));
    CHECK(event.kind == TRACE_RUN);
    CHECK(!scheduler_trace_query(&scheduler, (struct object_id){999, 1}, 0, &event));
    CHECK(scheduler_step(&scheduler, 1) == 1);
    CHECK(memories[1][0] == 1);
    CHECK(scheduler_step(&scheduler, 1) == 1);
    CHECK(memories[2][0] == 1);
    CHECK(scheduler_step(&scheduler, 1) == 0);
    handler = failing_handler;
    CHECK(scheduler_step(&scheduler, 2) == -1);
    CHECK(memories[0][0] == 1);
    CHECK(components[0].exited == 1);
    CHECK(components[0].metrics.invocations == 2);
    CHECK(components[0].metrics.cycles == 20);
    CHECK(components[1].mailbox_used == 0);
    CHECK(domain.lineage.count == 0);
    handler = successful_handler;
    CHECK(scheduler_step(&scheduler, 2) == 1);
    CHECK(memories[1][0] == 2);
    handler = NULL;
    reload();
    CHECK(memories[0][0] == 1);
    CHECK(components[1].mailbox_used == 0);
    scheduler_set_current(NULL);
}

static void test_capacity_and_provenance(void) {
    setup();
    CHECK(capability_domain_begin(&domain));
    struct capability_table before = components[1].capability_table;
    components[1].mailbox_used = COMPONENT_MAILBOX_BYTES;
    CHECK(capability_domain_send(&domain, &components[0], 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == JANI_ENOSPC);
    CHECK(memcmp(&before, &components[1].capability_table, sizeof(before)) == 0);
    CHECK(domain.lineage.count == 0);
    capability_domain_abort(&domain);
    CHECK(capability_domain_begin(&domain));
    uint32_t slot;
    while (component_capability_insert(&components[1], (struct object_id){99, components[1].capability_count + 1},
                                       CAP_RIGHT_READ, 0, &slot)) { }
    CHECK(capability_domain_send(&domain, &components[0], 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == JANI_ENOSPC);
    CHECK(components[1].mailbox_used == 0);
    capability_domain_abort(&domain);
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[0], 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == 0);
    CHECK(components[1].capability_table.slots[1].rights == CAP_RIGHT_READ);
    const uint8_t data[] = {1};
    for (uint32_t i = 0; i < OBJECT_STORE_PUT_BATCH_MAX; i++) {
        struct object_store_put_request request = {{30, i + 1}, {0, 1}, {0, 0}, {0, 0}, 0, data, 1};
        CHECK(capability_domain_write(&domain, &request));
    }
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_REJECTED);
    CHECK(components[1].mailbox_used == 0 && domain.lineage.count == 0);
    CHECK(object_table_find(&store.table, (struct object_id){30, 1}) == NULL);
    CHECK(object_store_delete(&store, (struct object_id){3, 1}));
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[0], 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == JANI_ENOENT);
    capability_domain_abort(&domain);

    setup();
    CHECK(capability_domain_begin(&domain));
    for (uint32_t i = 0; i < 10; i++) {
        components[0].logical_time = i;
        CHECK(capability_domain_use(&domain, &components[0], 0, CAP_RIGHT_READ, PROVENANCE_READ) == 0);
    }
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    reload();
    struct provenance_event event;
    CHECK(provenance_query(&domain.provenance, (struct object_id){3, 1}, 0, &event));
    CHECK(event.logical_time == 9);
    CHECK(provenance_query(&domain.provenance, (struct object_id){3, 1}, 7, &event));
    CHECK(event.logical_time == 2);
    CHECK(!provenance_query(&domain.provenance, (struct object_id){3, 1}, 8, &event));
    for (uint32_t i = 1; i < PROVENANCE_OBJECTS; i++) CHECK(provenance_append(&domain.provenance,
        (struct object_id){33, i}, components[0].root_id, 0, PROVENANCE_READ, JANI_EPERM));
    CHECK(!provenance_append(&domain.provenance, (struct object_id){33, 99}, components[0].root_id, 0, PROVENANCE_READ, 0));
    uint8_t bytes[PROVENANCE_BYTES];
    CHECK(provenance_encode(&domain.provenance, bytes, sizeof(bytes)));
    CHECK(!provenance_validate(bytes, sizeof(bytes) - 1));
    bytes[24] ^= 1;
    CHECK(!provenance_validate(bytes, sizeof(bytes)));
}

static void test_uninstall_and_stale_mail(void) {
    setup();
    struct object_id roots[3] = {components[0].root_id, components[1].root_id, components[2].root_id};
    CHECK(component_registry_store(&store, roots, 3, 13));
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_send(&domain, &components[0], 1, NULL, 0, 0, CAP_RIGHT_READ, 0) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    CHECK(capability_domain_begin(&domain));
    CHECK(capability_domain_revoke(&domain, &components[0], 0) == 0);
    CHECK(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    reload();
    uint32_t slot, attachment;
    CHECK(component_capability_insert(&components[1], (struct object_id){22, 1}, CAP_RIGHT_WRITE, 0, &slot));
    CHECK(slot == 1);
    memcpy(&attachment, components[1].mailbox + 4, 4);
    CHECK(attachment == UINT32_MAX);
    CHECK(capability_domain_uninstall(&domain, roots[0]));
    CHECK(component_set_find(&set, roots[0]) == NULL);
    size_t count;
    uint64_t sequence;
    CHECK(component_registry_load(&store, roots, 3, &count, &sequence));
    CHECK(count == 2 && sequence == 13);
    CHECK(domain.lineage.count == 0);
}

static unsigned live_calls;
static int recorded_handler(struct component *c) {
    uint8_t input = ((uint8_t *)c->instance)[0], output = 0;
    uint64_t args[8] = {input};
    int64_t result = 0;
    struct replay_session *s = replay_current();
    int mode = replay_before(s, 3, args, &input, 1, &output, 1, &result);
    if (mode < 0) return 0;
    if (mode == 1) {
        live_calls++;
        output = 42;
        result = 1;
        if (!replay_after(s, 3, args, &input, 1, &output, 1, result)) return 0;
    }
    ((uint8_t *)c->instance)[0]++;
    ((uint8_t *)c->instance)[1] = output;
    return result == 1;
}

static void test_replay(void) {
    static struct replay_session session;
    struct scheduler scheduler;
    setup();
    CHECK(scheduler_init(&scheduler, &domain, 1, 1000, test_clock));
    handler = recorded_handler;
    components[0].timer_armed = 1;
    CHECK(capability_domain_begin(&domain));
    CHECK(!replay_start(&session, &components[0]));
    capability_domain_abort(&domain);
    CHECK(replay_start(&session, &components[0]));
    CHECK(scheduler_step(&scheduler, 1) == 1);
    CHECK(live_calls == 1 && memories[0][1] == 42);
    CHECK(replay_verify(&session));
    CHECK(live_calls == 1);
    memories[0][2] ^= 1;
    CHECK(!replay_verify(&session));
    memories[0][2] ^= 1;
    CHECK(replay_save(&session, &store));
    replay_stop(&session);
    mount_store(0);
    memories[0][2] = 77;
    CHECK(replay_load(&session, &components[0]));
    CHECK(replay_verify(&session));
    CHECK(live_calls == 1 && memories[0][2] == 77);
    CHECK(!replay_log_validate(session.bytes, session.used - 1));
    session.bytes[8] ^= 1;
    CHECK(!replay_log_validate(session.bytes, session.used));
    session.bytes[8] ^= 1;
    session.mode = REPLAY_PLAY;
    session.cursor = 96;
    session.sequence = 1;
    uint64_t args[8] = {99};
    uint8_t in = 0, out = 0;
    int64_t result;
    CHECK(replay_before(&session, 3, args, &in, 1, &out, 1, &result) == -1);
    replay_stop(&session);
    CHECK(replay_start(&session, &components[0]));
    session.used = REPLAY_BYTES;
    CHECK(replay_before(&session, 3, args, &in, 1, &out, 1, &result) == -1);
    CHECK(!replay_save(&session, &store));
    replay_stop(&session);
    handler = NULL;
    scheduler_set_current(NULL);
}

static void test_hot_swap(void) {
    setup();
    struct component *c = &components[0];
    const uint8_t replacement[] = {2}, malformed[] = {0xff}, missing_handler[] = {0xfe};
    memset(memories[0] + 32, 0x5a, 16);
    CHECK(component_mailbox_push(c, (const uint8_t *)"pending", 7, -1));
    CHECK(component_commit(&store, c));
    CHECK(service_bind(c, 1, 3, 32, 16));
    CHECK(!service_bind(c, 1, 3, 32, 16));
    CHECK(!service_swap(c, malformed, 1, 1, 3));
    CHECK(!service_swap(c, missing_handler, 1, 1, 3));
    CHECK(!service_swap(c, replacement, 1, 2, 3));
    CHECK(!service_swap(c, replacement, 1, 1, 1));
    struct component original = *c;
    memcpy(baseline_disk, disk, sizeof(disk));
    writes = 0;
    CHECK(service_swap(c, replacement, 1, 1, 7));
    unsigned long total = writes;
    CHECK(c->mailbox_used == original.mailbox_used);
    CHECK(((uint8_t *)c->instance)[32] == 0x5a);
    CHECK(((uint8_t *)c->instance)[255] == 2);
    CHECK(service_rollback(c));
    CHECK(((uint8_t *)c->instance)[32] == 0x5a);
    CHECK(((uint8_t *)c->instance)[255] == 0);
    jani_wasm_instance_destroy(c->module, c->instance, c->exec_env, c->module_bytes);
    for (unsigned long cut = 0; cut <= total; cut++) {
        memcpy(disk, baseline_disk, sizeof(disk));
        *c = original;
        reload();
        writes_allowed = cut;
        int swapped = service_swap(c, replacement, 1, 1, 7);
        if (object_store_requires_recovery(&store)) CHECK(domain.halted);
        writes_allowed = ULONG_MAX;
        mount_store(0);
        struct component restored;
        CHECK(component_resume(&store, c->root_id, &restored));
        int new_module = restored.module_id.high == 9;
        CHECK(((uint8_t *)restored.instance)[255] == (new_module ? 2 : 0));
        CHECK(((uint8_t *)restored.instance)[32] == 0x5a);
        CHECK(restored.mailbox_used == original.mailbox_used);
        if (swapped) CHECK(new_module);
        const uint8_t *bytes;
        size_t size;
        struct object_header h;
        CHECK(object_store_get(&store, (struct object_id){8, c->root_id.low}, &h, &bytes, &size));
        CHECK(service_binding_validate(bytes, size));
        struct service_binding binding;
        memcpy(&binding, bytes, sizeof(binding));
        CHECK(object_id_equal(binding.module, restored.module_id));
        CHECK(binding.generation == (new_module ? 2 : 1));
        component_release(&restored);
        if (swapped) jani_wasm_instance_destroy(c->module, c->instance, c->exec_env, c->module_bytes);
    }
    *c = original;
}

extern int block_descriptors_validate(const uint8_t *bytes, size_t length, uint32_t operation);
static void test_hostile_boundaries(void) {
    uint32_t descriptors[9] = {0, 16, 0, 512, 512, 1, 16, 1, 1};
    CHECK(block_descriptors_validate((uint8_t *)descriptors, sizeof(descriptors), 0));
    CHECK(!block_descriptors_validate((uint8_t *)descriptors, sizeof(descriptors), 1));
    descriptors[5] = 0;
    CHECK(block_descriptors_validate((uint8_t *)descriptors, sizeof(descriptors), 1));
    descriptors[3] = UINT32_MAX;
    CHECK(!block_descriptors_validate((uint8_t *)descriptors, sizeof(descriptors), 1));
    CHECK(!block_descriptors_validate(NULL, sizeof(descriptors), 1));
    CHECK(!block_descriptors_validate((uint8_t *)descriptors, 0, 4));
    CHECK(instance_state_size(SIZE_MAX, 1) == 0);
    uint8_t bytes[64] = {0};
    struct instance_state_header header = {0};
    header.magic = INSTANCE_STATE_MAGIC;
    header.format_version = 1;
    header.header_size = 64;
    header.memory_size = UINT64_MAX - 63;
    memcpy(bytes, &header, sizeof(header));
    CHECK(!instance_state_header_validate(bytes, sizeof(bytes), NULL, NULL));
}

int main(void) {
    test_lineage();
    test_delivery();
    test_delivery_crashes();
    test_scheduler();
    test_capacity_and_provenance();
    test_uninstall_and_stale_mail();
    test_replay();
    test_hot_swap();
    test_hostile_boundaries();
    printf("test_phase4: %lu checks passed\n", checks_passed);
    return 0;
}
