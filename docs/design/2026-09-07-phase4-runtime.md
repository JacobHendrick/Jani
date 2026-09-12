# Phase 4 runtime

This record describes the bounded Phase 4 implementation, not a production
isolation guarantee. The kernel and WAMR still execute in Ring 0.

## Handler transactions and authority

One capability domain owns a store and all registered components. Boot resumes
the complete registry before loading the authoritative lineage graph. References
resolve by component root ID, slot, and generation. Local and remote derivations
share the same persisted graph; revocation removes the full descendant set.

The scheduler snapshots component metadata and the selected component's linear
memory before invoking a handler. Object writes are copied into owned staging
buffers. Reads consult those buffers first. Mailbox, capability, lineage, state,
provenance, and trace changes publish through one `object_store_put_many` batch.
The receiver cannot run until that batch commits. Failed handlers restore the
snapshots and stop their interpreter; remount recreates it from durable state.
An uncertain WAL result stops the domain until remount.

This revises the earlier delivery design: there is no separate receiver delivery
sequence. Sender progress and receiver delivery share the same table generation,
so recovery cannot retain a delivery while losing the sender's committed progress.
Two intentional sends are still two messages. Serial logging is an external
effect: a line printed before a failed commit can appear again after recovery.

The original `message_send` import retains its Phase 3 signature. New guests use
`message_send_cap` to specify attenuated rights and a badge. Neither API accepts
a sender's slot number as a receiver attachment without allocating a receiver
slot. Revocation strips queued attachments to revoked slots before slot reuse.

## Scheduler and observation

The scheduler runs one handler at a time. Pending mailboxes and due timers make
a component runnable. A rotating cursor breaks equal-priority ties; bounded
aging prevents a background priority from permanently winning every tie.
Interactive reservations must fit the frame budget before they are admitted.
Background work is admitted only when its remaining declared budget fits after
the unspent interactive reservations.

TSC measurements charge observed handler cycles after return. This is cooperative
budget admission, not hard real-time preemption: a handler can overrun its cycle
budget before the instruction limit stops it, and synchronous storage commits
also consume time. The existing ten-million-instruction WAMR limit remains.

`stats` returns four little-endian u64 counters: invocations, observed cycles,
successful runtime allocation requests in bytes, and messages sent. Allocation
bytes are cumulative requests, including reallocations, not live heap occupancy.
Counters describe the current instance lifetime and reset on remount. Failed
handlers retain observed CPU/allocation costs; the send counter counts accepted
send calls, not independently durable delivery receipts.

`trace` returns a caller-owned 40-byte event: component ID, logical time, value,
kind, reserved zero. Events cover run, wait, allocation, failure, and swap.
Wait values distinguish mailbox wait (1), timer wait (2), and waiting for
admission or a scheduling turn (3). Wait transitions and failure/swap events
enter the in-memory ring immediately; the next successful handler persists it.

The provenance object has a bounded per-object ring of authorization decisions,
including denied object writes. Its records identify component, operation,
logical time, and result. This is a diagnostic ledger, not a cryptographic audit
trail. Aborted handler records are rolled back with the handler.

## Recording and replay

A recording starts between handlers for one persistent component. It captures
initial state, handler boundaries, syscall arguments, bounded input bytes,
results, and output bytes with strictly increasing sequence numbers. Input is
copied before a syscall can overwrite an aliased output span. Playback invokes
a fresh instance and supplies recorded syscall results without repeating object
writes, sends, or console output. Every final linear-memory byte must match.

Saving a recording atomically stores its initial image, event log, original
module, and expected final image. Loading it later can verify against that saved
image even if the live component has changed. The kernel control API owns
recording and replay; ordinary guests cannot select another component to inspect.
The private hardware-driver instances are not recordable persistent actors.

A malformed record, changed argument/input, missing event, or full recorder fails
closed. A recording of a handler that could not commit cannot be saved as a
successful execution. The recorder is a bounded debugging facility, not an
unlimited flight recorder.

## Service replacement

A service binding is an IDL-checked, versioned object keyed by the stable
component root ID. It names the current and previous module, a state-schema ID,
supported message bits, generation, and the mutable state region. Installation
metadata is trusted: matching schema IDs are a declaration of compatibility,
not proof that arbitrary replacement code implements the same semantics.
The milestone's ping message and counter-state layouts are in `idl/records.idl`.

Replacement is allowed only between handlers. It requires the same state schema,
a message-set superset, valid zero-argument/zero-result handler exports, and a
state region fitting both memories. Only that region moves; the replacement
retains its own initialized data segments. Stable component identity preserves
capabilities and lineage. The pending mailbox is retained byte for byte.

One four-object batch publishes the new module, root, instance image, and service
binding. A failed batch destroys the candidate, retaining the old instance.
Rollback reinstalls the previous code while retaining current mutable state and
mailbox. It does not undo arbitrary application data changes made by that code.
Old module objects are retained; storage capacity limits the number of upgrades.

## Storage driver boundary

VirtIO read/write/flush request construction, data movement, status handling,
and completion run in `components/block_driver/driver.zig`. A separate Zig
supervisor restarts a trapped driver with a new capability generation. Only that
specific driver instance may access its bounded DMA buffer and submit requests;
ordinary components cannot obtain authority by guessing the numeric slot.

Implementation refinement: PCI discovery, feature negotiation, MMIO, and the
generic split-ring transport remain in the kernel. This is a checked transport
broker, not the blueprint's eventual interrupt-only stub. The driver cannot
submit arbitrary physical addresses: a Zig validator checks descriptor layout,
and C checks operation and sector against the pending request before translating
buffer offsets. Queue timeout disables the transport rather than reusing memory
while DMA completion is uncertain. There is no IOMMU protection.

The restart proof deliberately traps the driver between requests, re-creates it
through the supervisor, compares disk bytes, and checks the object store. It
does not claim that arbitrary failures during in-flight DMA are recoverable.
See the [VirtIO 1.3 specification](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html)
for the underlying block and split-ring protocol.

## Bounds and verification

Current bounds are four persistent components, 16 capability slots per component,
64 lineage edges, eight changed objects per transaction, a 4096-byte mailbox,
32 provenance objects with eight events each, 64 trace events, and a 64 KiB replay
log. Exhaustion returns an error or aborts the handler before publication.
These bounds are explicit limits, not a claim of unrestricted scaling.

`make test-phase4` covers delegation/remount/revocation, capacity rollback,
provenance, scheduler admission, replay mismatch, hot-swap rollback, and every
simulated write cut during delivery and replacement. `make phase4-negative`
must reject four seeded defects. `make phase4-demo` boots real WASM modules on
a temporary QEMU disk, checks all milestone markers, and restores the normal
ISO. Its serial log is `build/phase4-demo.log`. Existing persistence, zero-install,
crash-recovery, IDL, sanitizer, and fuzz gates remain required.
