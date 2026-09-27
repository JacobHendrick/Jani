#include "demo.h"
#include "../arch/interrupts.h"
#include "entropy.h"
#include "keys.h"
#include "peer.h"
#include "migration.h"
#include "../drivers/pit.h"
#include "../drivers/net_component.h"
#include "../lib/printk.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"

static struct net_identity identity;
static struct net_peer peer;
static struct migration migration;
static struct component imported_component;
static int seed_entropy(void *context, uint8_t *out, size_t length) {
    if (context == NULL || length != 32) return 0;
    memcpy(out, context, length); return 1;
}
static int equal(const void *a, const void *b, size_t length) {
    const uint8_t *left = a, *right = b;
    for (size_t i = 0; i < length; i++) if (left[i] != right[i]) return 0;
    return 1;
}
static int report_digest(struct object_id title, struct object_id set) {
    struct net_crdt state;
    uint8_t bytes[NET_CRDT_BYTES * 2], digest[32];
    if (replica_read(&peer.replica, title, &state) != 1 ||
        net_crdt_encode(bytes, NET_CRDT_BYTES, &state) != NET_CRDT_BYTES ||
        replica_read(&peer.replica, set, &state) != 1 ||
        net_crdt_encode(bytes + NET_CRDT_BYTES, NET_CRDT_BYTES, &state) != NET_CRDT_BYTES) return 0;
    crypto_blake2b(digest, sizeof(digest), bytes, sizeof(bytes));
    const char hex[] = "0123456789abcdef"; char text[65];
    for (size_t i = 0; i < 32; i++) { text[2*i] = hex[digest[i] >> 4]; text[2*i+1] = hex[digest[i] & 15]; }
    text[64] = 0;
    printk("phase5: converged digest %s\n", text);
    return 1;
}

static int receive_migration(void *context, const uint8_t *packet, size_t length,
    uint8_t *reply, size_t capacity, size_t *reply_length) {
    return migration_receive(context,packet,length,reply,capacity,reply_length);
}
static size_t send_migration(void *context, uint8_t *out, size_t capacity) {
    return migration_next_packet(context,out,capacity);
}
static int forwarded(struct object_store *store, struct component *component) {
    struct object_header header; const uint8_t *bytes; size_t length;
    struct migration_receipt receipt;
    return object_store_get(store,(struct object_id){MIGRATION_RECEIPT_HIGH,component->root_id.low},
        &header,&bytes,&length) && header.type_id.high == 0 &&
        header.type_id.low == MIGRATION_TYPE_RECEIPT &&
        migration_receipt_decode(bytes,length,&receipt) && receipt.forward_sequence != 0;
}

int phase5_network_demo(struct object_store *store, const struct net_boot_config *config,
    struct component *component, struct component_set *components, struct scheduler *scheduler) {
    if (store == NULL || config == NULL || config->role > 1 || components == NULL ||
        scheduler == NULL || scheduler->domain == NULL) return 0;
    interrupt_set_timer_logging(0);
    uint8_t secret[32];
    if (net_identity_open(store, seed_entropy, (void *)config->seed, &identity) != 1 ||
        !equal(identity.public_key, config->local_key, 32) ||
        !net_cpu_entropy(NULL, secret, sizeof(secret))) {
        kputs("PHASE5 FAIL: identity or fresh entropy unavailable\n"); return 0;
    }
    struct jani_udp_frame_fields route = {
        .source_mac = {0x52,0x54,0,0,0,1}, .destination_mac = {0x52,0x54,0,0,0,2},
        .source_ip = {10,0,0,1}, .destination_ip = {10,0,0,2},
        .source_port = 5555, .destination_port = 5555, .ttl = 64};
    if (config->role == 1) {
        route.source_mac[5] = 2; route.destination_mac[5] = 1;
        route.source_ip[3] = 2; route.destination_ip[3] = 1;
    }
    int started = net_peer_start(&peer, store, &identity, config->peer_key, secret, &route);
    crypto_wipe(secret, sizeof(secret));
    if (!started) { kputs("PHASE5 FAIL: peer start\n"); return 0; }
    if (!net_component_start()) { kputs("PHASE5 FAIL: protocol worker\n"); return 0; }
    peer.validate_packet = net_component_validate;
    struct object_id title = {config->role == 0 ? peer.replica.local_namespace : peer.replica.peer_namespace, 1};
    struct object_id set = {title.high, 2};
    if (config->role == 0 && peer.replica.catalog.count == 0) {
        struct object_id created;
        if (replica_create(&peer.replica, 1, NET_CRDT_LWW, &created) != 1 ||
            replica_set(&peer.replica, title, (const uint8_t *)"initial", 7) != 1 ||
            replica_share(&peer.replica, title, 3) != 1 ||
            replica_create(&peer.replica, 2, NET_CRDT_ORSET, &created) != 1 ||
            replica_add(&peer.replica, set, 1) != 1 ||
            replica_share(&peer.replica, set, 3) != 1) return 0;
    }
    if (!migration_open(&migration,store,&peer.session,config->role == 1) ||
        !net_peer_extension(&peer,&migration,receive_migration,send_migration)) return 0;
    struct component *target = NULL;
    int restored_fence = 0;
    if (config->role == 0) {
        for (size_t i = 0; i < store->table.count; i++) {
            struct object_id id = store->table.entries[i].id;
            if (id.high == COMPONENT_FENCE_ID_HIGH) {
                if (!migration_resume_source(&migration,(struct object_id){2,id.low})) return 0;
                restored_fence = 1;
                kputs("phase5: durable source fence restored\n"); break;
            }
        }
        if (!restored_fence && (component == NULL || component->instance == NULL)) return 0;
    } else {
        for (uint32_t i = 0; i < COMPONENT_MAX; i++)
            if (components->items[i] != NULL) components->items[i]->exited = 1;
        for (size_t i = 0; i < store->table.count; i++) {
            struct object_id id = store->table.entries[i].id;
            if (id.high != MIGRATION_RECEIPT_HIGH) continue;
            struct component *candidate = component_set_find(components,(struct object_id){2,id.low});
            if (candidate != NULL && migration_attach_target(&migration,candidate)) {
                target = candidate; target->exited = 0;
                kputs("phase5: imported component resumed from disk\n"); break;
            }
        }
    }

    uint64_t start = pit_get_ticks(), offline_until = 0, offline_after = 0;
    uint64_t progress_tick = 0;
    unsigned int authenticated = 0, online_edit = 0, offline_edit = 0;
    unsigned int network_done = 0, forward_sent = migration.record.forward_sequence != 0, timer_runs = 0;
    uint64_t handler_tick = pit_get_ticks(), logical_time = target != NULL ? target->logical_time :
        component != NULL ? component->logical_time : 0;
    for (;;) {
        __asm__ volatile ("sti; hlt");
        uint64_t ticks = pit_get_ticks();
        if (ticks - start > 20000) { kputs("PHASE5 FAIL: convergence timeout\n"); return 0; }
        if (offline_until == 0 || ticks >= offline_until) {
            if (net_peer_step(&peer, ticks) != 1) { kputs("PHASE5 FAIL: quarantined peer\n"); return 0; }
        }
        if (!peer.session.active) continue;
        if (!authenticated) {
            const char *hex = "0123456789abcdef"; char sid[65], node[65];
            for (size_t i=0;i<32;i++) {
                sid[2*i]=hex[peer.session.session_id[i]>>4]; sid[2*i+1]=hex[peer.session.session_id[i]&15];
                node[2*i]=hex[identity.node_id[i]>>4]; node[2*i+1]=hex[identity.node_id[i]&15];
            }
            sid[64]=node[64]=0;
            printk("phase5: node %s\nphase5: session %s\nphase5: pinned peer authenticated\n",node,sid);
            authenticated=1;
        }
        struct net_crdt register_state, set_state;
        if (replica_read(&peer.replica, title, &register_state) != 1 ||
            replica_read(&peer.replica, set, &set_state) != 1) continue;
        uint32_t local = peer.replica.local_node, remote = 1-local;
        if (config->role == 1 && register_state.vector[local] >= 1) online_edit = 1;
        if (!offline_edit && register_state.vector[local] >= 2) {
            if (set_state.vector[local] < 2 && (config->role == 0 ?
                replica_remove(&peer.replica,set,1) : replica_add(&peer.replica,set,3)) != 1) return 0;
            offline_edit = 1; offline_until = ticks;
            kputs("phase5: resumed replicated checkpoint\n");
        }
        if (config->role == 1 && !online_edit && register_state.vector[remote] == 1) {
            if (replica_set(&peer.replica, title, (const uint8_t *)"live edit", 9) != 1 ||
                replica_add(&peer.replica, set, 2) != 1) return 0;
            online_edit = 1; continue;
        }
        if (offline_after == 0 && register_state.vector[0] >= 1 && register_state.vector[1] >= 1 &&
            set_state.count >= 2) {
            kputs("phase5: live collaborative edit observed\n");
            offline_after = ticks + 1000;
        }
        if (!offline_edit && offline_after != 0 && ticks >= offline_after) {
            const char *value = config->role == 0 ? "offline A" : "offline B";
            if (replica_set(&peer.replica, title, (const uint8_t *)value, 9) != 1) return 0;
            if ((config->role == 0 ? replica_remove(&peer.replica, set, 1) :
                replica_add(&peer.replica, set, 3)) != 1) return 0;
            offline_edit = 1; offline_until = ticks + 500;
            kputs("phase5: edited while transport offline\n"); continue;
        }
        if (!network_done && offline_edit && ticks >= offline_until &&
            register_state.vector[0] == 2 && register_state.vector[1] == 2 &&
            set_state.vector[0] == 2 && set_state.vector[1] == 2 &&
            set_state.count == 3) {
            unsigned int removed = 0, live = 0;
            for (uint32_t i = 0; i < set_state.count; i++) {
                if (set_state.dots[i].removed && set_state.dots[i].value == 1) removed++;
                else if (!set_state.dots[i].removed &&
                    (set_state.dots[i].value == 2 || set_state.dots[i].value == 3)) live++;
            }
            if (removed == 1 && live == 2) {
                if (!report_digest(title, set)) return 0;
                kputs("PHASE5 NETWORK PASS\n"); network_done = 1;
            }
        }

        if (!network_done) continue;
        if (ticks >= progress_tick) {
            printk("phase5: transfer source=%d phase=%d offset=%d total=%d\n",
                (int)migration.source,(int)migration.phase,(int)migration.received,(int)migration.total);
            progress_tick=ticks+500;
        }
        if (config->role == 0 && !migration.source && logical_time >= 3) {
            if (migration_begin(&migration,component) != 1) {
                kputs("PHASE5 FAIL: source handoff\n"); return 0;
            }
            kputs("phase5: source durably fenced between handlers\n");
        }
        if (config->role == 1 && target == NULL && !object_id_is_zero(migration.imported)) {
            target = component_set_find(components,migration.imported);
            if (target == NULL) {
                if (!component_resume(store,migration.imported,&imported_component) ||
                    !component_set_add(components,&imported_component)) return 0;
                target = &imported_component; target->domain = scheduler->domain;
            }
            if (!migration_attach_target(&migration,target)) return 0;
            target->exited = 0; logical_time = target->logical_time;
            kputs("phase5: counter imported without initializer\n");
        }
        int run = config->role == 0 ? !migration.source :
            target != NULL && (timer_runs < 3 || target->mailbox_used != 0);
        if (run && ticks - handler_tick >= 100) {
            struct component *running = config->role == 0 ? component : target;
            int was_timer = running->mailbox_used == 0;
            if (logical_time == UINT64_MAX) return 0;
            logical_time++; handler_tick = ticks;
            if (scheduler_step(scheduler,logical_time) != 1) {
                kputs("PHASE5 FAIL: migrated handler\n"); return 0;
            }
            if (config->role == 1 && was_timer) timer_runs++;
        }
        if (config->role == 0 && migration.phase == 3) {
            if (!forward_sent) {
                if (migration_forward(&migration,(const uint8_t *)"migrated ping",13) != 1) return 0;
                forward_sent = 1; kputs("phase5: mailbox forwarding queued durably\n");
            } else if (migration_forward_pending(&migration) == 0) {
                kputs("phase5: forwarding acknowledged once\n");
                for (unsigned int i = 0; i < 500; i++) {
                    __asm__ volatile ("sti; hlt");
                    if (net_peer_step(&peer,pit_get_ticks()) != 1) return 0;
                }
                kputs("PHASE5 PASS\n"); return 1;
            }
        }
        if (config->role == 1 && target != NULL && timer_runs == 3 &&
            target->mailbox_used == 0 && forwarded(store,target)) {
            kputs("phase5: migrated counter and forwarded mailbox committed\n");
            /* Allow the sender to consume a retried acknowledgement before exit. */
            for (unsigned int i = 0; i < 500; i++) {
                __asm__ volatile ("sti; hlt");
                if (net_peer_step(&peer,pit_get_ticks()) != 1) return 0;
            }
            kputs("PHASE5 PASS\n"); return 1;
        }
    }
}
