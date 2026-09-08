#include "domain.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../wasm/instance_state.h"
#include "../wasm/runtime.h"

int component_mailbox_validate(const uint8_t *bytes, size_t length);

static int strip_attachment(struct component *c, uint32_t slot) {
#if defined(JANI_HOSTED) && defined(JANI_PHASE4_BUG_REVOKE)
    return 0;
#endif
    uint32_t offset = 0;
    int changed = 0;
    while (offset < c->mailbox_used) {
        uint32_t header[2];
        memcpy(header, c->mailbox + offset, sizeof(header));
        if (header[1] == slot) {
            uint32_t absent = UINT32_MAX;
            memcpy(c->mailbox + offset + 4, &absent, 4);
            changed = 1;
        }
        offset += 8 + header[0];
    }
    return changed;
}

static int member(const struct capability_domain *domain, const struct component *component) {
    if (domain == NULL || domain->components == NULL || component == NULL) return -1;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        if (domain->components->items[i] == component) return (int)i;
    }
    return -1;
}

static struct capability_ref reference(const struct component *component, uint32_t slot) {
    return capability_ref_make(component->root_id, slot,
        capability_table_generation(&component->capability_table, slot));
}

const struct capability *capability_domain_resolve(
    const struct capability_domain *domain, struct capability_ref ref) {
    struct component *component;
    if (domain == NULL || !capability_ref_is_valid(&ref)) return NULL;
    component = component_set_find(domain->components, ref.component_id);
    if (component == NULL || capability_table_generation(&component->capability_table,
        ref.slot) != ref.generation) return NULL;
    return capability_table_get(&component->capability_table, ref.slot);
}

static int graph_matches(struct capability_domain *domain) {
    for (uint32_t i = 0; i < domain->lineage.count; i++) {
        const struct capability_derivation *edge = &domain->lineage.records[i];
        const struct capability *p = capability_domain_resolve(domain, edge->parent);
        const struct capability *c = capability_domain_resolve(domain, edge->child);
        if (p == NULL || c == NULL || !capability_allows(p, CAP_RIGHT_GRANT) ||
            !object_id_equal(p->object, c->object) || (p->rights & c->rights) != c->rights) return 0;
    }
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        struct component *c = domain->components->items[i];
        if (c == NULL) continue;
        if (c->store != domain->store || !capability_table_is_valid(&c->capability_table)) return 0;
        for (uint32_t s = 0; s < CAP_TABLE_SLOTS; s++) {
            struct capability_ref parent;
            uint32_t p = c->capability_table.parents[s];
            int found = capability_derivation_parent(&domain->lineage, reference(c, s), &parent);
            if (p != CAP_SLOT_NONE && (!found || !capability_ref_equal(parent, reference(c, p)))) return 0;
            if (found && object_id_equal(parent.component_id, c->root_id) && p != parent.slot) return 0;
        }
    }
    return 1;
}

int capability_domain_open(struct capability_domain *domain,
                           struct object_store *store, struct component_set *components) {
    if (domain == NULL || store == NULL || components == NULL || object_store_requires_recovery(store)) return 0;
    memset(domain, 0, sizeof(*domain));
    domain->store = store;
    domain->components = components;
    if (object_table_find(&store->table, (struct object_id){4, 0}) != NULL) {
        if (!capability_lineage_load(store, &domain->lineage)) return 0;
    } else {
        uint8_t bytes[CAP_LINEAGE_BYTES];
        struct object_store_put_request request;
        /* Legacy tables have only local ancestry. Migrate it once, durably. */
        for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
            struct component *c = components->items[i];
            if (c == NULL) continue;
            if (!capability_table_is_valid(&c->capability_table)) return 0;
            for (uint32_t s = 0; s < CAP_TABLE_SLOTS; s++) {
                uint32_t p = c->capability_table.parents[s];
                if (p != CAP_SLOT_NONE && !capability_derivation_add(&domain->lineage,
                    reference(c, p), reference(c, s))) return 0;
            }
        }
        if (!capability_lineage_encode(&domain->lineage, bytes, sizeof(bytes))) return 0;
        request = capability_lineage_request(bytes);
        if (object_store_put_many(store, &request, 1) != OBJECT_STORE_BATCH_COMMITTED) return 0;
    }
    if (!graph_matches(domain)) return 0;
    if (object_table_find(&store->table, (struct object_id){5, 0}) != NULL &&
        !provenance_load(store, &domain->provenance)) return 0;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        if (components->items[i] != NULL) components->items[i]->domain = domain;
    }
    return 1;
}

int capability_domain_begin(struct capability_domain *domain) {
    if (domain == NULL || domain->active || domain->halted ||
        object_store_requires_recovery(domain->store)) return 0;
    domain->saved = kmalloc(sizeof(struct component) * COMPONENT_MAX);
    if (domain->saved == NULL) return 0;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        if (domain->components->items[i] != NULL) domain->saved[i] = *domain->components->items[i];
    }
    domain->saved_lineage = domain->lineage;
    domain->saved_provenance = domain->provenance;
    domain->provenance_dirty = 0;
    domain->tables_dirty = domain->states_dirty = domain->lineage_dirty = 0;
    domain->write_count = 0;
    domain->active = 1;
    return 1;
}

static void finish(struct capability_domain *domain) {
    for (uint32_t i = 0; i < domain->write_count; i++) kfree((void *)domain->writes[i].payload);
    domain->write_count = 0;
    kfree(domain->saved);
    domain->saved = NULL;
    domain->active = 0;
}

void capability_domain_abort(struct capability_domain *domain) {
    if (domain == NULL || !domain->active) return;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        if (domain->components->items[i] != NULL) *domain->components->items[i] = domain->saved[i];
    }
    domain->lineage = domain->saved_lineage;
    domain->provenance = domain->saved_provenance;
    finish(domain);
}

void capability_domain_state_changed(struct capability_domain *domain, struct component *c) {
    int i = member(domain, c);
    if (i >= 0 && domain->active) domain->states_dirty |= 1u << i;
}

int capability_domain_write(struct capability_domain *domain,
                             const struct object_store_put_request *request) {
    uint32_t i;
    uint8_t *copy;
    if (domain == NULL || !domain->active || request == NULL ||
        request->payload == NULL || request->payload_size == 0) return 0;
    for (i = 0; i < domain->write_count; i++) {
        if (object_id_equal(domain->writes[i].id, request->id)) break;
    }
    if (i == OBJECT_STORE_PUT_BATCH_MAX) return 0;
    copy = kmalloc(request->payload_size);
    if (copy == NULL) return 0;
    memcpy(copy, request->payload, request->payload_size);
    if (i < domain->write_count) kfree((void *)domain->writes[i].payload);
    else domain->write_count++;
    domain->writes[i] = *request;
    domain->writes[i].payload = copy;
    return 1;
}

int capability_domain_read(struct capability_domain *domain, struct object_id id,
                            struct object_header *header, const uint8_t **payload, size_t *size) {
    if (domain == NULL || header == NULL || payload == NULL || size == NULL || domain->halted) return 0;
    if (domain->active) {
        for (uint32_t i = 0; i < domain->write_count; i++) {
            const struct object_store_put_request *r = &domain->writes[i];
            if (!object_id_equal(r->id, id)) continue;
            memset(header, 0, sizeof(*header));
            header->id = id;
            header->type_id = r->type_id;
            header->creator_id = r->creator_id;
            *payload = r->payload;
            *size = r->payload_size;
            return 1;
        }
    }
    return object_store_get(domain->store, id, header, payload, size);
}

enum object_store_batch_result capability_domain_commit(struct capability_domain *domain) {
    struct object_store_put_request requests[OBJECT_STORE_PUT_BATCH_MAX];
    uint8_t *allocated[OBJECT_STORE_PUT_BATCH_MAX] = {0};
    uint8_t lineage[CAP_LINEAGE_BYTES];
    uint8_t *ledger = NULL;
    uint32_t count;
    enum object_store_batch_result result = OBJECT_STORE_BATCH_REJECTED;
    if (domain == NULL || !domain->active) return result;
    count = domain->write_count;
    memcpy(requests, domain->writes, count * sizeof(requests[0]));
    if (!graph_matches(domain)) goto done;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        struct component *c = domain->components->items[i];
        if (c == NULL) continue;
        if (c->capabilities_dirty || (domain->tables_dirty & (1u << i))) {
            if (count == OBJECT_STORE_PUT_BATCH_MAX) goto done;
            uint8_t *buffer = kmalloc(COMPONENT_CAPTABLE_BYTES);
            if (buffer == NULL) goto done;
            allocated[count] = buffer;
            if (!component_captable_encode(c, buffer, COMPONENT_CAPTABLE_BYTES)) goto done;
            requests[count++] = (struct object_store_put_request){
                c->captable_id, {0, COMPONENT_TYPE_CAPTABLE}, c->root_id, c->root_id,
                c->logical_time, buffer, COMPONENT_CAPTABLE_BYTES};
        }
        if (domain->states_dirty & (1u << i)) {
            uint8_t *memory;
            size_t size, written;
            if (count == OBJECT_STORE_PUT_BATCH_MAX ||
                !jani_wasm_instance_memory(c->instance, &memory, &size)) goto done;
            size_t needed = instance_state_size(size, c->mailbox_used);
            uint8_t *buffer = kmalloc(needed);
            if (buffer == NULL) goto done;
            allocated[count] = buffer;
            if (!instance_state_serialize(c, memory, size, buffer, needed, &written)) goto done;
            requests[count++] = (struct object_store_put_request){
                c->state_id, {0, COMPONENT_TYPE_INSTANCE_STATE}, c->root_id, c->root_id,
                c->logical_time, buffer, written};
        }
    }
    if (domain->lineage_dirty) {
        if (count == OBJECT_STORE_PUT_BATCH_MAX ||
            !capability_lineage_encode(&domain->lineage, lineage, sizeof(lineage))) goto done;
        requests[count++] = capability_lineage_request(lineage);
    }
    if (domain->provenance_dirty) {
        if (count == OBJECT_STORE_PUT_BATCH_MAX || (ledger = kmalloc(PROVENANCE_BYTES)) == NULL ||
            !provenance_encode(&domain->provenance, ledger, PROVENANCE_BYTES)) goto done;
        requests[count++] = provenance_request(ledger);
    }
    result = count == 0 ? OBJECT_STORE_BATCH_COMMITTED : object_store_put_many(domain->store, requests, count);
done:
    kfree(ledger);
    for (uint32_t i = 0; i < OBJECT_STORE_PUT_BATCH_MAX; i++) kfree(allocated[i]);
    if (result == OBJECT_STORE_BATCH_COMMITTED) {
        for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
            if (domain->components->items[i] != NULL) domain->components->items[i]->capabilities_dirty = 0;
        }
        finish(domain);
    } else {
        capability_domain_abort(domain);
        if (result == OBJECT_STORE_BATCH_RECOVERY_REQUIRED) domain->halted = 1;
    }
    return result;
}

int capability_domain_derive(struct capability_domain *domain, struct component *owner,
                             uint32_t slot, uint32_t rights, uint32_t badge) {
    struct capability_table staged;
    struct capability_derivation_table graph;
    uint32_t child;
    int i = member(domain, owner);
    if (i < 0 || !domain->active) return JANI_EINVAL;
    staged = owner->capability_table;
    graph = domain->lineage;
    if (!capability_table_derive(&staged, slot, rights, badge, &child)) return JANI_EPERM;
    struct capability_ref child_ref = capability_ref_make(owner->root_id, child, staged.generations[child]);
    if (!capability_derivation_add(&graph, reference(owner, slot), child_ref)) return JANI_ENOSPC;
    owner->capability_table = staged;
    if (child >= owner->capability_count) owner->capability_count = child + 1;
    domain->lineage = graph;
    domain->lineage_dirty = 1;
    domain->tables_dirty |= 1u << i;
    return (int)child;
}

int capability_domain_use(struct capability_domain *domain, struct component *owner,
                          int32_t slot, uint32_t rights, uint32_t operation) {
    const struct capability *capability;
    int result;
    if (member(domain, owner) < 0 || !domain->active) return JANI_EINVAL;
    capability = capability_table_get(&owner->capability_table, (uint32_t)slot);
    result = capability == NULL ? JANI_EINVAL :
        (capability_allows(capability, rights) ? 0 : JANI_EPERM);
    if (!provenance_append(&domain->provenance,
            capability == NULL ? owner->root_id : capability->object,
            owner->root_id, owner->logical_time, operation, result)) return JANI_ENOSPC;
    domain->provenance_dirty = 1;
    return result;
}

int capability_domain_send(struct capability_domain *domain, struct component *sender,
                           uint32_t target, const uint8_t *payload, uint32_t length,
                           int32_t source, uint32_t rights, uint32_t badge) {
    struct component *receiver;
    struct capability_table staged;
    struct capability_derivation_table graph;
    const struct capability *destination;
    int32_t slot = -1;
    uint32_t child = 0;
    if (member(domain, sender) < 0 || !domain->active || length > 4096 ||
        (length != 0 && payload == NULL) || source < -1) return JANI_EINVAL;
    destination = capability_table_get(&sender->capability_table, target);
    if (!capability_allows(destination, CAP_RIGHT_SEND)) return JANI_EPERM;
    receiver = component_set_find(domain->components, destination->object);
    if (receiver == NULL || receiver->exited) return JANI_ENOENT;
    staged = receiver->capability_table;
    graph = domain->lineage;
    if (source == -1) {
        if (rights != 0 || badge != 0) return JANI_EINVAL;
    } else {
        struct capability capability;
        const struct capability *parent = capability_table_get(&sender->capability_table, (uint32_t)source);
        if (!capability_derive(parent, rights, badge, &capability)) return JANI_EPERM;
#if defined(JANI_HOSTED) && defined(JANI_PHASE4_BUG_GRANT)
        capability.rights = CAP_RIGHT_ALL;
#endif
        struct object_header header;
        const uint8_t *bytes;
        size_t size;
        if (!capability_domain_read(domain, parent->object, &header, &bytes, &size)) return JANI_ENOENT;
        if (!capability_table_insert_root(&staged, &capability, &child)) return JANI_ENOSPC;
        if (sender == receiver) staged.parents[child] = (uint32_t)source;
        struct capability_ref child_ref = capability_ref_make(receiver->root_id, child, staged.generations[child]);
        if (!capability_derivation_add(&graph, reference(sender, (uint32_t)source), child_ref)) return JANI_ENOSPC;
        slot = (int32_t)child;
    }
    if (!component_mailbox_push(receiver, payload, length, slot)) return JANI_ENOSPC;
    if (slot >= 0) {
        receiver->capability_table = staged;
        if (child >= receiver->capability_count) receiver->capability_count = child + 1;
        domain->tables_dirty |= 1u << member(domain, receiver);
        domain->lineage = graph;
        domain->lineage_dirty = 1;
    }
    capability_domain_state_changed(domain, receiver);
    return 0;
}

int capability_domain_revoke(struct capability_domain *domain,
                             struct component *owner, uint32_t slot) {
    struct capability_ref removed[CAP_DERIVATION_MAX + 1];
    struct capability_derivation_table graph;
    size_t count;
    if (member(domain, owner) < 0 || !domain->active ||
        capability_table_get(&owner->capability_table, slot) == NULL) return JANI_EINVAL;
    graph = domain->lineage;
    removed[0] = reference(owner, slot);
    if (!capability_derivation_remove_subtree(&graph, removed[0], &removed[1], CAP_DERIVATION_MAX, &count)) return JANI_EINVAL;
    count++;
    /* Remove the incoming edge too, preventing a dangling reference on reuse. */
    for (uint32_t i = 0; i < graph.count; i++) {
        if (capability_ref_equal(graph.records[i].child, removed[0])) {
            memmove(&graph.records[i], &graph.records[i + 1], (graph.count - i - 1) * sizeof(graph.records[0]));
            memset(&graph.records[--graph.count], 0, sizeof(graph.records[0]));
            break;
        }
    }
    for (size_t n = 0; n < count; n++) {
        if (capability_domain_resolve(domain, removed[n]) == NULL) return JANI_EINVAL;
        struct component *c = component_set_find(domain->components, removed[n].component_id);
        if (!component_mailbox_validate(c->mailbox, c->mailbox_used)) return JANI_EINVAL;
    }
    for (size_t n = 0; n < count; n++) {
        struct component *c = component_set_find(domain->components, removed[n].component_id);
        uint32_t s = removed[n].slot;
        if (strip_attachment(c, s)) capability_domain_state_changed(domain, c);
        memset(&c->capability_table.slots[s], 0, sizeof(struct capability));
        c->capability_table.parents[s] = CAP_SLOT_NONE;
        while (c->capability_count && capability_table_get(&c->capability_table, c->capability_count - 1) == NULL) c->capability_count--;
        domain->tables_dirty |= 1u << member(domain, c);
    }
    domain->lineage = graph;
    domain->lineage_dirty = 1;
    return 0;
}

int capability_domain_uninstall(struct capability_domain *domain, struct object_id id) {
    struct component *owner;
    if (domain == NULL || domain->active || (owner = component_set_find(domain->components, id)) == NULL ||
        !capability_domain_begin(domain)) return 0;
    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        if (capability_table_get(&owner->capability_table, slot) != NULL &&
            capability_domain_revoke(domain, owner, slot) != 0) {
            capability_domain_abort(domain);
            return 0;
        }
    }
    if (capability_domain_commit(domain) != OBJECT_STORE_BATCH_COMMITTED ||
        !component_uninstall(domain->store, id)) return 0;
    component_set_remove(domain->components, id);
    component_release(owner);
    return 1;
}
