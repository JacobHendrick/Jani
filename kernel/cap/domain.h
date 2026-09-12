#ifndef JANI_KERNEL_CAP_DOMAIN_H
#define JANI_KERNEL_CAP_DOMAIN_H

#include "lineage.h"
#include "provenance.h"
#include "../wasm/component_set.h"

/* One domain owns all registered components on one store. Handlers are serial. */
struct capability_domain {
    struct object_store *store;
    struct component_set *components;
    struct capability_derivation_table lineage;
    struct capability_derivation_table saved_lineage;
    struct component *saved;
    uint32_t tables_dirty;
    uint32_t states_dirty;
    uint32_t lineage_dirty;
    uint32_t active;
    uint32_t halted;
    struct provenance_ledger provenance;
    struct provenance_ledger saved_provenance;
    uint32_t provenance_dirty;
    struct object_store_put_request writes[OBJECT_STORE_PUT_BATCH_MAX];
    uint32_t write_count;
};

int capability_domain_open(struct capability_domain *domain,
                           struct object_store *store, struct component_set *components);
const struct capability *capability_domain_resolve(
    const struct capability_domain *domain, struct capability_ref reference);
int capability_domain_begin(struct capability_domain *domain);
void capability_domain_abort(struct capability_domain *domain);
enum object_store_batch_result capability_domain_commit(struct capability_domain *domain);
int capability_domain_write(struct capability_domain *domain,
                             const struct object_store_put_request *request);
int capability_domain_read(struct capability_domain *domain, struct object_id id,
                            struct object_header *header, const uint8_t **payload, size_t *size);
void capability_domain_state_changed(struct capability_domain *domain, struct component *component);
int capability_domain_derive(struct capability_domain *domain, struct component *owner,
                             uint32_t slot, uint32_t rights, uint32_t badge);
int capability_domain_send(struct capability_domain *domain, struct component *sender,
                           uint32_t target, const uint8_t *payload, uint32_t length,
                           int32_t source, uint32_t rights, uint32_t badge);
int capability_domain_revoke(struct capability_domain *domain,
                             struct component *owner, uint32_t slot);
int capability_domain_uninstall(struct capability_domain *domain, struct object_id owner);
int capability_domain_use(struct capability_domain *domain, struct component *owner,
                          int32_t slot, uint32_t rights, uint32_t operation);

#endif
