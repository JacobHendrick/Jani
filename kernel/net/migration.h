#ifndef JANI_KERNEL_NET_MIGRATION_H
#define JANI_KERNEL_NET_MIGRATION_H
#include "session.h"
#include "../wasm/component.h"

#define MIGRATION_MAX_BYTES (1024u * 1024u)
#define MIGRATION_HEADER_BYTES 128u
#define MIGRATION_WIRE_BYTES 96u
#define MIGRATION_CHUNK_BYTES (NET_BODY_MAX - MIGRATION_WIRE_BYTES)
#define MIGRATION_RECORD_BYTES 176u
#define MIGRATION_RECEIPT_BYTES 128u
#define MIGRATION_RECEIPT_HIGH UINT64_C(0x4a414e494d494752)
#define MIGRATION_TYPE_FENCE UINT64_C(0x5010)
#define MIGRATION_TYPE_RECEIPT UINT64_C(0x5011)
#define MIGRATION_TYPE_OUTBOX UINT64_C(0x5012)
#define MIGRATION_OUTBOX_HIGH UINT64_C(0x4a414e494d49474f)
#define MIGRATION_FORWARD_MAX 256u

struct migration_record {
    uint8_t peer[32], digest[32];
    struct object_id root, module, state, captable;
    uint64_t generation;
    struct object_id target;
    uint64_t forward_sequence;
};
struct migration_bundle_view {
    struct object_id root;
    uint64_t generation;
    uint32_t module_length, state_length, captable_length, reserved;
};
struct migration_wire_view {
    uint32_t opcode, offset, total, length;
    uint64_t generation;
    uint8_t digest[32];
    struct object_id root, target;
};
struct migration_receipt {
    uint8_t peer[32], digest[32];
    struct object_id source;
    uint64_t generation;
    struct object_id target;
    uint64_t forward_sequence;
};
struct migration {
    struct object_store *store;
    struct net_session *session;
    struct migration_record record;
    struct component *target_component;
    uint8_t *bundle;
    size_t total, received;
    uint32_t source, phase, allow_incoming, halted;
    struct object_id imported;
};

_Static_assert(sizeof(struct migration_record) == 160, "migration record ABI");
_Static_assert(sizeof(struct migration_receipt) == 112, "migration receipt ABI");
_Static_assert(sizeof(struct migration_wire_view) == 88, "migration wire ABI");
_Static_assert(sizeof(struct migration_bundle_view) == 40, "migration bundle ABI");

int migration_wire_decode(const uint8_t *, size_t, uint32_t, struct migration_wire_view *);
int migration_record_decode(const uint8_t *, size_t, struct migration_record *);
int migration_receipt_decode(const uint8_t *, size_t, struct migration_receipt *);
int migration_bundle_validate(const uint8_t *, size_t, const uint8_t peer[32],
    const uint8_t local[32], struct migration_bundle_view *);
int migration_open(struct migration *, struct object_store *, struct net_session *, int allow_incoming);
/* Trusted coordinator, between handlers. Only self READ/SEND capabilities,
 * no cross-component lineage or native references, may cross this boundary.
 * Source stays stopped on an uncertain commit. Returns 1, 0, or -1 (remount). */
int migration_begin(struct migration *, struct component *);
int migration_resume_source(struct migration *, struct object_id root);
size_t migration_next_packet(struct migration *, uint8_t *, size_t);
int migration_receive(struct migration *, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
/* Caller owns imported runtime. Attach only after component_resume succeeds. */
int migration_attach_target(struct migration *, struct component *);
int migration_forward_pending(struct migration *);
int migration_forward(struct migration *, const uint8_t *, size_t);
void migration_close(struct migration *);
#endif
