# Durable capability delivery

Status: implemented. The handler transaction revision below was made during
Phase 4 integration on 2026-09-07.

## Goal

A successful message attachment changes three durable facts:

1. the frame in the receiver mailbox;
2. the child capability in the receiver table;
3. the parent-to-child edge in the global lineage graph.

Those facts must appear together after remount. A crash may preserve the old
state or the new state, but never a subset.

## Delegation semantics

An attachment is a delegated copy. The sender keeps its parent capability and
the receiver gets a new child capability.

The sender supplies the child rights and badge. The source capability must own
`GRANT`, and the requested rights must be a non-empty subset of the source
rights. The child may omit `GRANT`; this is the normal way to send authority
that cannot be delegated again. A capability without `GRANT` cannot be the
source of an attachment.

The new `message_send_cap` IDL adds rights and badge arguments. The original
`message_send` import remains compatible and does not attach capabilities.
When no capability
is attached, the optional slot is `-1` and both values must be zero. Self-send
uses the same rules as cross-component send. A numeric sender slot is never
placed directly in a mailbox.

## Transaction boundary

The object table is already a copy-on-write root. A single WAL record publishes
one complete table generation, so durable delivery uses one bounded
`object_store_put_many()` call rather than a second journal format.

Delivery stages three replacement objects:

- receiver instance state, containing the new mailbox frame;
- receiver capability table;
- global lineage table.

Revision: publication happens at the sender's handler boundary, together with
its memory, consumed input, changed capability tables, object writes, and
audit records. Publishing only the receiver inside the syscall would allow a
crash to deliver a message while rolling the sender back to before its send.
The single-threaded scheduler does not run a receiver before the sender's
transaction commits. Within a handler, reads see staged writes and sends can
see earlier staged messages. Struct snapshots and a linear-memory snapshot
provide rollback; failed interpreters are stopped and must be recreated.

The store writes every new object version and one candidate table generation,
flushes them, then publishes that table with the existing WAL root. The WAL and
superblock disk formats do not change.

The first API is put-only and accepts one through eight unique object IDs.
Eight is a transaction limit, not a promise that every four-component mutation
fits. A handler touching too many distinct records is rejected and rolled back
as a whole. Delete support is deferred until a caller requires
atomic puts and deletes in the same generation.

## Store results

`OBJECT_STORE_BATCH_REJECTED` means the new table root was not published.
Validation, duplicate IDs, capacity, allocation, and failures known to occur
before WAL publication return this state. Staged in-memory component copies are
discarded.

`OBJECT_STORE_BATCH_COMMITTED` means the new table root and cleanup are
durable. Only then may another handler observe its mailbox, capability-table,
and lineage changes.

`OBJECT_STORE_BATCH_RECOVERY_REQUIRED` means the WAL write or later cleanup
reported an error. The WAL sector may still be durable, so rollback is unsafe.
The store rejects reads and mutations until remount. Mount recovery selects the
old or new table root and rebuilds all live capability state from that root.

## Global lineage object

The graph is authoritative persistent state, not a cache reconstructed from
component tables. Local parent indexes cannot encode a parent owned by another
component, so reconstruction would lose cross-component ancestry.

The reserved lineage object ID is `(high=4, low=0)`. Its type ID is
`(high=0, low=7)`. The first format is fixed size:

- 64-bit magic;
- 32-bit format version;
- 32-bit active record count;
- 32-bit CRC32C over all record slots;
- 32 reserved zero bits;
- 64 fixed derivation records.

Unused records are all zero. A Zig `ReleaseSafe` validator checks byte count,
magic, version, reserved fields, checksum, reference validity, duplicate
children, cycles, and zeroed unused records before C adopts the bytes.

The graph records local and cross-component derivations. This gives revocation
one traversal instead of trying to merge two ancestry models.

## Slot identity

A capability reference is `(component root ID, slot, generation)`. The
existing reserved 32-bit field in `struct capability_ref` becomes the
generation, preserving the 24-byte reference size.

Capability-table format version 3 persists one generation per slot. Version 1
and version 2 occupied slots import with generation 1. Empty slots begin at
zero. Installing into an empty slot increments its generation before the slot
becomes visible. A generation may never wrap to zero; a slot at
`UINT32_MAX` is no longer reusable.

Lineage resolution requires all three fields to match. An old edge therefore
cannot target a different capability that later occupies the same numeric
slot.

## Capacity and failure rules

Delivery validates the target, source slot, object existence, `GRANT`,
attenuation, payload span, receiver space, mailbox space, and graph space
before changing component state. The complete store batch is validated before
publication; a late capacity failure rolls back the whole handler.

A full mailbox, receiver table, or graph returns a bounded error without
installing a capability or message. Receiver delivery sequences are not added:
the sender's progress and receiver's delivery now share one atomic table root.
A remount either retains both or neither. This prevents duplicates caused by
handler rollback; it does not deduplicate two intentional application sends.

A handler runs only after delivery commits. Popping the message and changing
component memory become durable in the normal component commit. If the handler
fails before that commit, remount restores the message and it may run again.

## Revocation and uninstall

Revocation traverses the global graph using generation-bearing references,
stages every affected component table plus the reduced graph, and commits those
objects in one batch. Local parent fields remain as validation metadata, but
the global graph determines the complete descendant set.

Revocation also removes attachments naming revoked slots from queued frames,
without dropping their payloads. Reusing a slot cannot turn an old queued
attachment into authority over a different object.

Uninstall first revokes all nodes owned by the component and all descendants,
including remote descendants. It commits the affected capability tables and
lineage before the existing registry/module deletion sequence. A crash between
those stages may leave the component installed with less authority, which is
safe and retryable; it must never leave authority pointing at an uninstalled
owner.

## Verification

Hosted tests must cover all write cut points and prove remount exposes either
the complete old batch or the complete new batch. Capability integration adds
two-component delegation, denied amplification, full-capacity rollback,
generation reuse, hostile lineage bytes, remount, recursive remote revocation,
and uninstall cleanup tests. The milestone also requires a QEMU demonstration
of allowed delegation and denied unauthorized object access.
