# WAL commit/recovery TLA+ model — design

Date: 2026-07-15
Status: approved
Blueprint refs: Phase 2 "PROTOCOL MODEL FIRST", rule R9

## Purpose

Before writing a line of the Phase 2 write-ahead log, model the WAL
commit/recovery protocol in TLA+ and model-check the blueprint invariant:

> No committed object version is lost or torn after a crash at ANY step.

The model is the design authority for the C implementation. Per R9 it lives
in `docs/models/` and is updated whenever the protocol changes.

## Decisions (made 2026-07-15)

1. **Crash fidelity: full sector-level model.** A crash may leave any
   subset of unflushed sector writes on disk — this covers both torn
   multi-sector records and write reordering across a missing flush
   barrier, the two classes of design bug the model exists to catch.
2. **Scope: commit + recovery + checkpoint/truncation.** Checkpointing has
   its own crash bugs (truncating the WAL before checkpointed data is
   durable destroys committed versions), so it is in scope. Snapshots and
   generations are out of scope: they ride the same commit path and
   inherit its safety.
3. **Style: raw TLA+ actions** (not PlusCal). Crashes and reordering are
   nondeterminism, which the action style expresses directly, and it
   matches the published specs this work will be compared against.
4. **Method: two-spec refinement.** An abstract spec states the promise
   (atomic durable commits); the concrete spec models the protocol at
   sector level; TLC checks the concrete spec refines the abstract one.
   Inside the concrete spec, the disk uses the pending-set/subset-crash
   representation (decision 1).

## Files

```
docs/models/WalCommitAbstract.tla   the promise: atomic, durable commits
docs/models/WalCommit.tla           the protocol: sectors, WAL, checkpoint, crash
docs/models/WalCommit.cfg           TLC config: constants, invariants, refinement
docs/models/MCWalCommit.tla         model-check wrapper (state constraints), if needed
third_party/tla2tools.jar           vendored TLC (runs on installed system Java)
Makefile                            model-check and model-check-negative targets
```

## Abstract spec: `WalCommitAbstract.tla` (~40 lines)

State:
- `store` — map `object -> sequence of committed versions`
- `acked` — set of operations whose commit has been acknowledged

Actions:
- `AtomicCommit(op)` — store and acked update in a single step
- `AbstractCrash` — in-flight unacked operations may vanish; `store` and
  `acked` are untouched

There is no reachable state in which a version is partial, and no step
removes an acked operation. This spec *is* the invariant, stated as a
state machine.

## Concrete spec: `WalCommit.tla` (~250–300 lines)

### Disk representation

- `durable` — function `sector -> value` (what is on the platter)
- `pending` — set of `<<sector, value>>` writes issued but not flushed
- `Flush` — applies all of `pending` to `durable`, empties `pending`
- `Crash` — nondeterministically applies **any subset** of `pending` to
  `durable`, discards the rest, enters recovery. Set semantics collapses
  every possible write arrival order into "which subset landed", giving
  reordering and torn records without modeling a queue.

### Protocol actions (mirroring the future C code)

1. `WriteVersionData` — new version payload written to a free extent
   (copy-on-write: live data is never overwritten in place)
2. `AppendWalRecord` — the record spans 1–2 WAL sectors. "Checksum valid"
   is modeled as: all of the record's sectors are durable and mutually
   consistent.
3. `FlushWal` — the barrier. `AckCommit` is enabled only after it: this
   is the commit point.
4. `ApplyToTable` — lazily write the object-table home location (post-ack)
5. `Checkpoint` — flush outstanding table-home writes, then write a new
   WAL-start pointer to the superblock, flush, then truncate the log.
   This ordering is a primary verification target.
6. `RecoverScan` — reads durable state only: scan the WAL from the
   superblock pointer, replay valid records in order, stop at the first
   torn/invalid record; everything after it is ignored.

### Refinement mapping

Abstract `store` == what `RecoverScan` would reconstruct from the current
durable state; abstract `acked` == the concrete ack history (a history
variable). TLC checks `WalCommit => WalCommitAbstract!Spec`: every
sector-level behavior, including mid-crash and mid-recovery states, is
indistinguishable from the atomic machine.

### Invariants

- `TypeOK` — shapes of all variables
- `NoTornVersionVisible` — no readable object version is partially written
- `DurableRecoverable` — recovery from durable state alone reproduces the
  committed history. This subsumes checkpoint-orphan safety: advancing the
  superblock past an un-homed record makes the invariant fail.
- Refinement property (above)

## Model-checking configuration

Small constants chosen so TLC terminates in minutes: 2 objects, 2 values,
~4 WAL sectors, ~4 data sectors; state constraint caps runs at 3
operations and 2 crashes. Constants live in `WalCommit.cfg` so they can be
raised for longer overnight checks.

## Negative validation (the model must be able to fail)

`make model-check-negative` checks known-broken protocol mutants and
asserts TLC **finds a counterexample** for each. Mutants are selected by
`BUGGY_*` boolean constants inside `WalCommit.tla` (no duplicated spec
files):

- `BUGGY_NO_WAL_FLUSH` — ack without the WAL flush barrier
- `BUGGY_TRUNCATE_FIRST` — checkpoint truncates before table writes are
  durable
- `BUGGY_SKIP_CHECKSUM` — recovery trusts a record without validating its
  parts and payload, so it replays through a tear instead of stopping.
  This was renamed during planning because continuing past a torn record in
  the linear, non-reused WAL is harmless; trusting the torn record is not.

If TLC cannot break a mutant, the model is too weak to trust.

## Workflow integration

- `make model-check` joins `make test` and `make fuzz-heap` as a standing
  gate. It (and `model-check-negative`) invokes vendored
  `third_party/tla2tools.jar` via system Java.
- Devlog entry when the model is green.
- Per R9, protocol changes during Phase 2 implementation must be
  reflected in the model before landing in C.

## Out of scope

- Snapshots/generations, garbage collection of old versions
- Disk corruption at rest (bit rot) — checksums in the C code handle
  detection; the model assumes sectors are either old, new, or absent
- Performance properties (liveness is checked only as basic "recovery
  terminates")
