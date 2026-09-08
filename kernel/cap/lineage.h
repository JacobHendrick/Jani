#ifndef JANI_KERNEL_CAP_LINEAGE_H
#define JANI_KERNEL_CAP_LINEAGE_H

#include "derivation.h"
#include "../obj/object_store.h"

#define CAP_LINEAGE_MAGIC UINT64_C(0x4A414E494C494E31)
#define CAP_LINEAGE_BYTES (24u + CAP_DERIVATION_MAX * CAPABILITY_DERIVATION_SIZE)

/* The validator accepts unaligned bytes and never adopts an invalid graph. */
int capability_lineage_validate(const uint8_t *bytes, size_t length);
int capability_lineage_encode(const struct capability_derivation_table *table,
                              uint8_t *bytes, size_t capacity);
int capability_lineage_decode(struct capability_derivation_table *table,
                              const uint8_t *bytes, size_t length);
int capability_lineage_load(struct object_store *store,
                            struct capability_derivation_table *table);
struct object_store_put_request capability_lineage_request(const uint8_t *bytes);

#endif
