#include "component.h"
#include "service.h"
#include "runtime.h"
#include "../sched/scheduler.h"
#include "../replay/record.h"
#include "../drivers/block_component.h"
#include "../lib/printk.h"
#include "../lib/string.h"

extern const uint8_t _binary_build_phase4_v1_wasm_start[], _binary_build_phase4_v1_wasm_end[];
extern const uint8_t _binary_build_phase4_v2_wasm_start[], _binary_build_phase4_v2_wasm_end[];
static struct component sender, receiver;
static struct component_set components;
static struct capability_domain domain;
static struct scheduler scheduler;
static struct replay_session recording;

static uint64_t clock_cycles(void) {
    uint32_t low, high;
    __asm__ volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return ((uint64_t)high << 32) | low;
}

#define VERIFY(expression) do { if (!(expression)) { printk("PHASE4 FAIL: line %d\n", __LINE__); return 0; } } while (0)

int phase4_demo(struct object_store *store, struct component *counter) {
    const uint8_t *v1 = _binary_build_phase4_v1_wasm_start;
    const uint8_t *v2 = _binary_build_phase4_v2_wasm_start;
    size_t v1_size = (size_t)(_binary_build_phase4_v1_wasm_end - v1);
    size_t v2_size = (size_t)(_binary_build_phase4_v2_wasm_end - v2);
    uint32_t data_slot, send_slot;
    const uint8_t data[] = {7};
    const struct service_ping ping = {0};
    VERIFY(component_install(store, v1, v1_size, &sender));
    VERIFY(component_install(store, v1, v1_size, &receiver));
    component_set_init(&components);
    VERIFY(component_set_add(&components, counter));
    VERIFY(component_set_add(&components, &sender));
    VERIFY(component_set_add(&components, &receiver));
    VERIFY(capability_domain_open(&domain, store, &components));
    VERIFY(object_store_put(store, (struct object_id){3, 999}, (struct object_id){0, COMPONENT_TYPE_DATA},
        sender.root_id, sender.root_id, 0, data, sizeof(data)));
    VERIFY(component_capability_insert(&sender, (struct object_id){3, 999}, CAP_RIGHT_ALL, 0, &data_slot));
    VERIFY(component_capability_insert(&sender, receiver.root_id, CAP_RIGHT_SEND, 0, &send_slot));
    VERIFY(capability_domain_begin(&domain));
    VERIFY(capability_domain_send(&domain, &sender, send_slot, (const uint8_t *)&ping, sizeof(ping), (int32_t)data_slot, CAP_RIGHT_READ, 42) == 0);
    VERIFY(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    VERIFY(scheduler_init(&scheduler, &domain, 1, UINT64_C(100000000000), clock_cycles));
    counter->timer_armed = 0;
    VERIFY(replay_start(&recording, &receiver));
    VERIFY(scheduler_step(&scheduler, 1) == 1);
    struct provenance_event event;
    VERIFY(provenance_query(&domain.provenance, (struct object_id){3, 999}, 0, &event));
    VERIFY(event.result == 0 && event.operation == PROVENANCE_READ);
    VERIFY(provenance_query(&domain.provenance, (struct object_id){3, 999}, 1, &event));
    VERIFY(event.result == JANI_EPERM && event.operation == PROVENANCE_WRITE);
    kputs("phase4: allowed and denied uses persisted in provenance\n");
    VERIFY(replay_verify(&recording));
    VERIFY(replay_save(&recording, store));
    uint8_t *memory;
    size_t memory_size;
    VERIFY(jani_wasm_instance_memory(receiver.instance, &memory, &memory_size));
    printk("phase4: replay memory identical, CRC32C %x\n", object_crc32c(memory, memory_size));
    replay_stop(&recording);
    VERIFY(replay_load(&recording, &receiver));
    VERIFY(replay_verify(&recording));
    replay_stop(&recording);
    kputs("phase4: persisted recording replayed\n");
    VERIFY(service_bind(&receiver, 1, 1, 49152, 16));
    VERIFY(capability_domain_begin(&domain));
    VERIFY(capability_domain_send(&domain, &sender, send_slot, (const uint8_t *)&ping, sizeof(ping), -1, 0, 0) == 0);
    VERIFY(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    uint32_t mailbox = receiver.mailbox_used;
    VERIFY(!service_swap(&receiver, v2, v2_size, 2, 1));
    VERIFY(service_swap(&receiver, v2, v2_size, 1, 1));
    VERIFY(receiver.mailbox_used == mailbox);
    kputs("phase4: hot-swap preserved pending mailbox\n");
    VERIFY(scheduler_step(&scheduler, 2) == 1);
    VERIFY(jani_wasm_instance_memory(receiver.instance, &memory, &memory_size));
    struct service_counter_state state;
    memcpy(&state, memory + 49152, sizeof(state));
    VERIFY(state.count == 3);
    VERIFY(service_rollback(&receiver));
    VERIFY(jani_wasm_instance_memory(receiver.instance, &memory, &memory_size));
    memcpy(&state, memory + 49152, sizeof(state));
    VERIFY(state.count == 3);
    kputs("phase4: service rollback succeeded\n");
    VERIFY(capability_domain_begin(&domain));
    VERIFY(capability_domain_revoke(&domain, &sender, data_slot) == 0);
    VERIFY(capability_domain_commit(&domain) == OBJECT_STORE_BATCH_COMMITTED);
    VERIFY(domain.lineage.count == 0);
    kputs("phase4: remote descendant revoked\n");
    VERIFY(block_component_crash_test());
    VERIFY(object_store_collect(store));
    kputs("PHASE4 PASS\n");
    return 1;
}
