# Jani OS agent guide

This file applies to the entire repository. It gives coding agents the current
implementation context and the constraints that are easiest to lose when
working from one source file at a time.

## Sources of truth

Read the smallest relevant set before changing code:

1. `README.md` describes what the public repository actually supports.
2. `SECURITY.md` defines the current threat model and its honest limitations.
3. `docs/architecture/blueprint.md` is the long-term design and phase order.
4. `docs/design/` contains decisions that constrain an active subsystem.
5. `docs/development/devlog.md` records what was built, what failed, and why.
6. `CONTRIBUTING.md` defines the required review and test discipline.

The blueprint is a plan, not proof that a feature exists. The source, tests,
and current devlog determine implementation status. If they disagree, do not
paper over the conflict; document and resolve it.

## Project in one paragraph

Jani is a freestanding x86_64 kernel built around a crash-consistent persistent
object store, capability-based authority, and event-driven WebAssembly
components. The intended model is one global kernel address space, with
isolation supplied by capability checks and WAMR rather than per-process page
tables. Persistent component state is stored as ordinary versioned objects so
a component can resume after power loss without an application-level save or
load operation.

## Current implementation state

### Completed foundations

- Phase 0: Limine BIOS/UEFI boot, serial diagnostics, GDT/IDT, exception
  reporting, PIC/PIT interrupts, and PS/2 keyboard input.
- Phase 1: physical and virtual memory managers, a reusable size-class kernel
  heap, hosted sanitizer tests, and heap fuzzing.
- Phase 2: sorted object table, immutable object versions, WAL recovery,
  snapshots and rollback, derived free-space bitmap, cache, garbage collection,
  modern polled VirtIO block I/O, a TLA+ WAL model, hosted crash injection, and
  QEMU crash testing.
- Phase 3: WAMR 2.4.5 classic interpreter, checked module and syscall
  boundaries, generated C/Zig syscall SDKs, persistent component state and
  mailboxes, install/resume/uninstall, deterministic-resume checks, and a
  module-free zero-install resume image.

### Phase 4 work already present

- A capability is an object ID, rights mask, and badge.
- Per-component tables contain 16 slots and local parent links.
- Derivation requires `GRANT` and may only attenuate rights.
- Local revocation recursively clears descendants.
- Capability tables persist slot generations in format version 3 and still
  read versions 1 and 2.
- `cap_derive`, `cap_revoke`, and `cap_drop` cross the generated WASM ABI.
- A bounded component set routes message payloads to running components.
- Cross-component capability attachments are still deliberately rejected.
- `kernel/cap/derivation.{c,h}` now provides a bounded 64-edge global lineage
  graph keyed by `(component root ID, local slot, generation)`.
- The global graph rejects malformed references, duplicate children, cycles,
  and overflow. Subtree removal is transitive and atomic when output capacity
  is insufficient.
- The graph is compiled into the kernel and has 130 focused hosted checks, but
  it is not yet connected to component mutation or persistent storage.
- The object store can publish up to eight object versions through one atomic
  table generation. Uncertain WAL outcomes quarantine access until remount.
- The durable delivery design selects copy delegation, generation-bearing slot
  references, a persisted lineage object, and one store batch for all changed
  records.
- WAMR handler instruction metering, 4096-byte syscall transfer limits, mailbox
  overflow hardening, W^X mapping checks, and CR0.WP are enabled.

At the slot-generation baseline, `make test` reports 9,339 checks,
`make kernel` links, and `make idl-check` reports no generated drift.

## The next implementation slice

Finish Phase 4 capability semantics before beginning the scheduler.

### Delivery design

`docs/design/2026-08-23-capability-delivery.md` is the approved record for
cross-component capability delivery. It answers the persistence, attenuation,
capacity, recovery, revocation, slot reuse, and uninstall questions. Keep the
implementation consistent with that record or revise the record explicitly.

### Expected implementation order

1. Add serialization and hostile-byte validation for the lineage format.
   Parsing or validating untrusted persistent bytes belongs in Zig.
2. Add component-set helpers that resolve a `capability_ref` to a live
   component and exact slot generation.
3. Record local derivations in the global graph as well as cross-component
   edges.
4. On delivery, allocate a new receiver slot. Never place the sender's numeric
   slot directly into the receiver mailbox.
5. Use the atomic store batch for mailbox, capability-table, and lineage
   changes. Every capacity and handler failure must leave no residual authority.
6. Extend revocation so a parent clears all local and remote descendants.
7. Add two-component hosted coverage, persistence/remount coverage, and a
   QEMU demonstration of allowed delegation plus denied unauthorized access.
8. Add the bounded per-object provenance ledger required by Phase 4 and show
   successful and denied uses in the milestone demo.

Only after the capability milestone is coherent should work proceed to the
round-robin scheduler, priorities, interactive budget admission, metering,
record/replay, trace/introspection, hot-swap, and the driver-component proof.

## Non-negotiable architecture invariants

- One address space. Do not introduce process page tables or a Unix process
  model. WAMR and capabilities are the current isolation substrate.
- Handles, not names. Kernel-facing component APIs use capability indexes or
  object IDs, never paths or user-facing string names.
- Components are event-driven actors. Invoke run-to-completion handlers and
  persist only between handlers; never snapshot a native interpreter stack.
- All nondeterminism enters through syscalls. Preserve this boundary so replay
  can log inputs and reproduce execution.
- Object versions are copy-on-write and checksummed before becoming reachable.
- Storage ordering is load-bearing. WAL data and commit records require their
  established flush sequence; a physical device honoring flush is still a
  hardware assumption.
- New mappings must not be writable and executable simultaneously.
- Capability derivation never amplifies rights. Revocation must include every
  descendant, including descendants owned by another component.
- Bounded structures must fail cleanly at capacity. No unbounded kernel growth
  is acceptable in a component-controlled path.
- WAMR currently executes in Ring 0. Runtime validation is not hardware
  isolation; never claim otherwise.

## Language and trust boundaries

- Keep kernel trunks and core algorithms in freestanding C11.
- Use fixed-width integers for formats and boundary-sensitive arithmetic.
- Use Zig `ReleaseSafe` modules for hostile or corrupt byte validation: disk
  records, WASM modules, syscall arguments, future message formats, and network
  frames.
- WASM components and first-party guest code are Zig by default.
- Assembly is reserved for CPU entry, interrupt transitions, and operations C
  cannot express.
- Do not casually modify `third_party/wamr`. It is a curated, pinned upstream
  subset; `make verify-wamr` proves all 75 vendored files match WAMR 2.4.5.
- Generated files are committed but never edited as the source of truth.
  Change `idl/*.idl` or the generator, run `make idl-generate`, then run
  `make idl-check` and `make idl-negative`.
- On-disk C structs remain hand-written and are asserted against generated IDL
  layout checks. A format change requires compatibility and recovery analysis.

## Code and review discipline

- Match the surrounding C layout and naming; compile with all warnings as
  errors.
- Check pointer validity, integer overflow, bounds, object existence, rights,
  and capacity before mutation.
- For multi-step mutation, either validate everything before the first write or
  provide an explicit rollback path tested at every failure point.
- Document allocation ownership for functions that allocate or retain memory.
- Test malformed input, zero and maximum sizes, one-past-the-limit values,
  resource exhaustion, duplicate operations, and corrupted persisted state.
- Add a negative control when introducing a security or durability gate. A
  check that has never rejected the seeded defect is not yet trusted.
- Keep changes focused. Do not combine format or trust-boundary changes with
  unrelated cleanup.
- Preserve user work and inspect `git status` before and after editing. Stage
  only the intended files.
- Append one concise entry to `docs/development/devlog.md` for each completed
  session, including the important failure or lesson and the gates run.

## Build and verification

The supported host is Fedora x86_64. The repository expects Zig 0.16.0 at
`third_party/zig/zig`; that directory is intentionally ignored by Git.

Minimum pre-review gates:

```sh
make check-tools
make test
make kernel
make iso
make idl-check
```

Run focused gates according to the change:

| Area | Required additional checks |
| --- | --- |
| IDL or ABI | `make idl-negative`, rebuild guest WASM |
| WAMR/vendor port | `make verify-wamr`, WASM fuzz targets |
| Heap | `make fuzz-heap` |
| Object store/WAL | `make fuzz-object-store`, `make crash-test`, `make write-ordering-negative` |
| Persistent components | `make wow-demo`, `make wow-demo-negative`, `make zero-install-test` |
| Boot, VMM, interrupts, drivers | `make run` and inspect serial output |

ASan leak detection may fail when the test process is itself run under a
ptrace-based harness. In that environment only, use
`ASAN_OPTIONS=detect_leaks=0 make test` and state that leak detection was
disabled. Do not make that the project default.

## Known traps worth preserving

- WAMR mutates module bytes during load and retains pointers into them. Each
  instance must own its module-byte copy for the instance lifetime.
- WAMR expects newly mapped linear memory to be zero-filled, including grown
  regions. The kernel platform mapping path must preserve that property.
- `kheap_used_bytes()` is a bump high-water mark, not a live-allocation count;
  a changed value alone does not prove a leak.
- The object cache's oversized-object guard prevents an infinite eviction loop
  when an object can never fit in the arena.
- The freestanding kernel's SSE/MMX flags were chosen from disassembly evidence;
  do not change CPU code-generation assumptions without inspecting the ELF.
- QEMU process termination is not a real power cut, and QEMU accepting flush is
  not proof that physical storage honors it.
- A generated drift check matters only when the generated artifact is actually
  compiled or otherwise adopted by the build.

## Repository map

- `kernel/arch`, `kernel/boot`: CPU and boot path.
- `kernel/mm`: physical/virtual memory and heap.
- `kernel/obj`: persistent objects, WAL, snapshots, cache, and validation.
- `kernel/cap`: capabilities, local tables, and global lineage foundation.
- `kernel/wasm`: WAMR port, components, instance state, syscalls, and routing.
- `kernel/drivers`: PCI, VirtIO, serial, PIT/PIC, and keyboard drivers.
- `components`: first-party Zig WASM components.
- `idl`: authoritative syscall and record declarations.
- `sdk/c`, `sdk/zig`: generated guest-facing ABI and wrappers.
- `tools/hosted`: sanitizer tests and fuzz harnesses.
- `docs/models`: formal WAL models.
- `docs/design`: subsystem decisions; add the capability-delivery design here.
- `docs/archive/plans`: completed implementation plans retained for history.
