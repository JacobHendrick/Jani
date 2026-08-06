# The wow demo — design

**Phase 3, slice 2 of 4.** Date: 2026-08-03.

## Goal

A counter component increments once per logical tick and prints over serial.
Power QEMU off entirely. Boot again. The counter resumes from exactly where it
was — no save, no load, no restore step, and no mechanism anywhere that could
perform one.

This is the blueprint's milestone demo and the single most important
demonstration in the project.

## Success criteria

- Serial shows the counter advancing, produced by a WASM module.
- `make wow-demo` powers the VM off mid-run, boots again, and the counter
  continues from its last committed value — not lower, and **not higher**.
- `make test`, `make kernel`, `make crash-test` stay green.
- `make write-ordering-negative` still detects a removed barrier.

"Not higher" is as load-bearing as "not lower". A double-counted tick and a
lost tick are both failures of the same invariant, and the design below exists
mostly to make both impossible.

## Non-goals

| Deferred | To |
|---|---|
| IDL v1, `tools/idlc`, `sdk/c`, `sdk/zig` | slice 3 |
| Zero-install, module objects from the store | slice 4 |
| Real capability rights, delegation, revocation | Phase 4 |
| Scheduler classes, metering, budgets | Phase 4 |
| Driver-as-component policy | Phase 4 |
| Dirty-page tracking for linear memory | see D3.10 |

## Decisions

### D3.5 — Flush durability is split into two claims, one of them tested now

The demo's whole claim is that power was cut and the state survived. Whether
flush is a real durability barrier was unverified as of 2026-08-02: with
`CACHE_MODE=unsafe`, where QEMU discards every flush, `make crash-test` still
passes, so that gate cannot distinguish a barrier from a no-op.

The question splits in two:

1. **Does the device honor the barrier?** Hardware. Needs a real machine and a
   real power cut. Jacob cannot verify hardware (per project documentation), so this
   remains a stated assumption, and the wow demo on real metal is its test.
2. **Does our code issue the barrier in the right places?** Software, and
   testable hosted today.

Claim 2 is now tested. `tools/hosted/test_write_ordering.c` models a disk with
two images — what a reader sees, and what survives power loss — where `flush`
is the only operation that promotes bytes between them. Power-cut points are
swept through a transaction and recovery is asserted to yield either the old
version or the new one, never a torn one.

Two negative controls prove the rig has teeth, mirroring how
`model-check-negative` sits beside `model-check`:

- Discard every flush: 48 of 48 cut points lose data. This is `CACHE_MODE=unsafe`
  reproduced hosted, and unlike the QEMU gate, this one notices.
- Drop exactly one barrier: 2 of 3,840 (flush, cut, drop) triples corrupt.

The second control required the model to cut power *after* a write succeeds
rather than at the write, and to keep a subset of unflushed writes rather than
discarding all of them. A dropped barrier alone is harmless because the next
flush repairs it atomically; corruption needs the commit record to reach the
platter while the data it references does not. A conservative power-cut model
cannot express that, and would have passed while proving nothing.

The store passed every version of this, including 240 cut × drop combinations. That is evidence about the *implementation* of the commit
protocol, which is what `WalCommit.tla` cannot supply.

**What remains assumed:** that the physical device honors a flush. Everything
in this slice rests on it, and only real hardware settles it.

### D3.6 — The full syscall surface is declared now, before the IDL

The beachhead spec deferred the IDL to slice 3 on the grounds that "a syscall
surface designed before anything has run is a guess." That reasoning is sound
and the risk is real. The surface below is nonetheless declared in full now,
for one reason that outweighs it.

The blueprint places the capability *engine* in Phase 4 but the
capability-shaped *syscalls* in Phase 3. Declaring all twelve now lets every
signature take a capability slot index from the start, backed in this slice by
a trivial identity table. When the real engine lands, it changes what an index
means, not what a signature is — so the ABI never breaks. Declaring three now
and nine later would mean designing the remaining nine *after* the engine
exists, which is the ordering that forces a break.

Slice 3's IDL pass is the scheduled place to correct a guess. It is
transcription only if the guesses held; that is a risk this slice accepts
explicitly rather than by omission.

### D3.7 — Commit is synchronous, once per handler return

A handler runs to completion, returns, and its state is made durable before
the next invocation is scheduled. No batching, no staleness window.

The alternative was a bounded async window, which is what a production system
eventually wants. It is rejected here on two grounds. It makes the demo's claim
approximate ("resumes within the window") when the entire point is that it is
exact. And the metering that would justify it — per-component cycle and
allocation counters — does not exist until Phase 4, so choosing it now would be
optimizing against a guess.

At one tick per second the cost is irrelevant. Batching can be added when there
is a measurement demanding it.

### D3.8 — The object graph splits by write frequency, not by role

A component instance is four objects plus a registry entry:

| Object | Contents | Written |
|---|---|---|
| Registry (`{1,0}`, well-known) | `next_sequence`, `component_count`, root IDs | On instantiate / destroy |
| Component root | format version, the three constituent IDs | Once, at instantiate |
| Module | raw `.wasm` bytes | Once, at instantiate |
| Capability table | slot array: object ID + rights + badge | Once, at instantiate (this slice) |
| **Instance state** | linear memory image, mailbox ring, logical clock, pending timer deadline | **Every tick, one atomic put** |

The blueprint lists linear memory and the mailbox as separate objects. They are
merged here, and the reason is correctness rather than tidiness.

A tick advances two pieces of state together: linear memory (`N` → `N+1`) and
scheduling state (clock `T` → `T+1`). As separate objects that is two `put`
calls, so two transactions, with a power-cut window between them. Neither
ordering survives it:

- Memory committed first, cut before the clock: `mem=N+1, clock=T`. On resume
  the tick at `T` fires again and the counter advances by **two**.
- Clock committed first, cut before memory: `mem=N, clock=T+1`. The tick at
  `T+1` is **lost** and the counter skips one.

Exactness requires atomicity. The two ways to get it are a multi-object
transaction in the store, or one object. A multi-object transaction means
extending the WAL and COW paths — the most safety-critical code in the project,
and the code that hid a committed-transaction-loss bug from a TLA+ model, the
full suite, and review. Merging costs nothing instead: memory and mailbox are
both dirty on every tick already, so one object writes exactly as many bytes as
two did.

Module and capability table stay separate because they are genuinely
write-once, which is the same criterion applied consistently. The split is by
write frequency throughout; role is not the axis.

### D3.9 — Discovery is a well-known root ID, and the registry is persisted

Boot finds components by reading the registry at the compile-time constant
`{1,0}` — one `object_store_get` of a known ID, then a walk of the root IDs it
lists. O(1), no superblock format change, no table-enumeration API.

This is persisted derived state, which the free-space bitmap rule appears to
forbid: the bitmap is derived at mount and never written, because "a torn
bitmap write could mark live data free; a derived one cannot tear." The
precedent is deliberately not followed here, and the reasons should be visible
so the difference does not read as an oversight.

The failure modes are not comparable. A torn bitmap silently hands live sectors
to a new allocation. A stale registry either fails to resume a component or
names one that is absent — both loud, neither silent corruption. More
decisively, the bitmap could not be tied to any single transaction because it
covers every allocation at once, whereas a registry entry is written in the
same transaction as the component it names and therefore cannot tear
independently of it.

ID allocation: `{0,N}` are well-known type IDs, `{1,0}` is the registry, and
every other object takes `{2, seq}` from the monotonic `next_sequence` in the
registry. No ID carries structural meaning, and the root stores its constituent
IDs explicitly rather than deriving them, so the layout can change later
without the derivation being load-bearing.

### D3.10 — Linear memory is copied whole; dirty-page tracking is deferred

At each handler return the full linear memory image is written into the
instance-state object. `os_mmap` stays exactly as it is today — kmalloc-backed
and zeroed on the way out, per the slice 1 invariant.

The blueprint's phrasing ("linear memory lives in object-space pages…
snapshotting is just the Phase 2 COW mechanism doing its normal job") describes
a dirty-page design. Reaching it requires a bit-6 accessor in the VMM, which
does not exist — `vmm.h` defines no dirty or accessed flag — and a partial-write
path in the store, which also does not exist, since `object_store_put` takes a
whole payload.

The deferral is free in the dimension that matters. **Both designs produce a
byte-identical object.** The instance-state payload is the same image either
way; dirty-page tracking changes how it is written, not what is stored. So
choosing the copy now commits no format and forecloses nothing, which matters
in a slice already committing the graph, the ABI, and the registry convention.

The cost of the alternative is not the MMU work but the store work, for the
same reason given in D3.8. And it cannot be justified on evidence yet: the
metering that would show whether 64 KB per tick matters arrives in Phase 4.

Upgrade path, when metering justifies it: add dirty/accessed accessors to the
VMM, back `os_mmap` with pages the instance-state object owns, and add a
partial-write path to the store. No format migration is required.

## The syscall surface

Twelve calls. Every one that touches an object takes a capability slot index,
never a raw object ID, so Phase 4's engine changes the meaning of an index
rather than any signature. In this slice the table is an identity mapping and
rights are recorded but not enforced.

| # | Call | Signature | Returns |
|---|---|---|---|
| 1 | `jani_log` | `(ptr: i32, len: i32)` | `i32` bytes written, or negative |
| 2 | `jani_object_create` | `(type_hi: i64, type_lo: i64, size: i32)` | `i32` slot, or negative |
| 3 | `jani_object_read` | `(slot: i32, offset: i32, ptr: i32, len: i32)` | `i32` bytes read, or negative |
| 4 | `jani_object_write` | `(slot: i32, offset: i32, ptr: i32, len: i32)` | `i32` bytes written, or negative |
| 5 | `jani_object_size` | `(slot: i32)` | `i64` size, or negative |
| 6 | `jani_cap_drop` | `(slot: i32)` | `i32` 0, or negative |
| 7 | `jani_message_send` | `(target: i32, ptr: i32, len: i32, cap: i32)` | `i32` 0, or negative |
| 8 | `jani_message_recv` | `(ptr: i32, len: i32, sender_out: i32, cap_out: i32)` | `i32` bytes, or negative |
| 9 | `jani_timer_set` | `(delay_ticks: i64)` | `i32` 0, or negative |
| 10 | `jani_time_logical` | `()` | `i64` current logical time |
| 11 | `jani_self` | `()` | `i32` slot of own root capability |
| 12 | `jani_exit` | `(code: i32)` | does not return |

`cap: -1` in `jani_message_send` means "no capability attached".

Errors are small negative constants: `JANI_EINVAL -1`, `JANI_EPERM -2`,
`JANI_ENOSPC -3`, `JANI_EAGAIN -4`, `JANI_ERANGE -5`, `JANI_ENOENT -6`.

`jani_log` replaces the slice 1 host function rather than extending it, as the
beachhead spec required.

### Exports the module must provide

| Export | Called |
|---|---|
| `jani_init` | Once, at first instantiation only. Never on resume. |
| `jani_on_message` | Mailbox non-empty |
| `jani_on_timer` | Pending deadline reached |
| `memory` | Standard WASM linear memory export |

`jani_init` not running on resume is what makes the demo true. A resumed
component is not re-initialized; it is continued.

### The determinism contract

Enforced in the WAMR port now, per the blueprint's warning that it is nearly
free at this stage and unpayable later:

- No syscall exposes wall-clock time, entropy, or a host address.
- `jani_time_logical` returns a counter, not a clock.
- NaN behaviour is pinned by measurement rather than by canonicalization —
  **amended 2026-08-06, see below.**

**Amendment, 2026-08-06.** The first bullet originally read "NaN
canonicalization enabled." That was never implemented, and vendored WAMR has no
build option for it: the classic interpreter's float opcodes expand to plain C
operators (`DEF_OP_NUMERIC(float32, float32, F32, /)` is a literal `/=` on
`float`), so NaN behaviour is whatever the host architecture does. What was
measured on x86-64 and is now pinned by `tools/hosted/test_determinism.c`:

- **NaN *produced* from non-NaN operands is already canonical and stable.**
  `0.0/0.0` and `inf-inf` both yield `0xFFC00000` (f32) and
  `0xFFF8000000000000` (f64) — payload MSB set, every other payload bit zero,
  which is exactly WASM's canonical NaN, whose sign bit the spec leaves
  unspecified. Identical across 4,096 repetitions.
- **NaN *propagated* through an operation keeps its payload.** `0x7FC01234 + 1.0`
  gives back `0x7FC01234`; a signalling NaN is quieted by setting bit 22 and
  otherwise preserved; with two NaN operands the first one wins. This is *not*
  canonicalization, and the WASM spec permits it precisely because it varies
  between implementations.

The contract still holds, but for a different reason than the original bullet
claimed. A payload can only enter a computation from the component's own
constants or its own linear memory — no syscall accepts or returns a float, so
there is **no host-side source of NaN payloads**. Every payload a component can
observe is one it produced itself from deterministic inputs, and linear memory
is snapshotted and restored bit-exactly, so resume reproduces exactly.

This reasoning depends on the engine, not just on the architecture, so the four
preconditions are now `_Static_assert`s in `kernel/wasm/runtime.c`: interpreter
on, AOT off, JIT off, fast-interp off. Enabling AOT or fast-interp fails the
kernel build with a message naming this document. WAMR forces JIT off whenever
AOT is off, so that assertion is guarded by the AOT one rather than independent.

What remains genuinely unaddressed: a component that reinterprets a
self-authored NaN payload as an integer would observe a value that a *different*
engine could compute differently. That is a portability limit, not a
reproducibility one, and it becomes real only if Jani ever gains a second
execution engine.

Wall time never enters a component's world. That is required by the
determinism contract anyway, and it dissolves the hardest question in
resume-after-power-off: a persisted wall-clock deadline that expired during
downtime has no correct interpretation, and logical time never poses the
question.

## Data flow

### Boot

1. Mount the store.
2. Read the registry at `{1,0}`.
3. **Absent** — first boot. Format path: create the registry, install the
   Limine-delivered module as a module object, create the root, capability
   table, and instance state, instantiate, call `jani_init`, commit.
4. **Present** — resume path. For each root ID: read the root, load the module
   object into WAMR, create an instance, overwrite its linear memory from the
   instance-state object, restore the mailbox, clock, and pending deadline.
   `jani_init` is **not** called.
5. Schedule from the restored deadline.

### Run loop

1. The PIT paces wall time. When the interval elapses, logical time advances.
2. If a deadline has been reached, invoke `jani_on_timer`; if the mailbox is
   non-empty, invoke `jani_on_message`. One handler at a time, run to
   completion, never preempted.
3. The handler returns. Serialize linear memory, mailbox, clock, and pending
   deadline into the instance-state object and `put` it — one transaction.
4. Only then is the next invocation eligible.

Step 3 completing before step 4 begins is the entire durability contract. A
handler's effects are either fully durable or fully absent.

## Verification

Staged serial output, one line per step, so a failure localizes itself:

```
store: mounted, registry found (1 component)
component: root {2,1}, module {2,2}, state {2,4}
component: resumed at logical time 41, counter 41
counter: 42
counter: 43
```

On first boot, `registry absent, formatting` and `component: initialized`
replace the resume line.

**Hosted, under ASan/UBSan:**

- `tools/hosted/test_write_ordering.c` — power-loss barrier semantics.
  Delivered. Positive sweep in `make test`; negative controls in
  `make write-ordering-negative`.
- `tools/hosted/test_component_state.c` — instance-state serialization round
  trip: memory, mailbox ring wraparound, clock, deadline. Truncated and
  oversized payloads rejected.
- `tools/hosted/test_syscall_args.c` — the Zig argument decoder against valid
  and malformed arguments, especially out-of-bounds pointer/length pairs and
  integer overflow in `offset + len`.
- `tools/hosted/fuzz_syscall_args.c` — `make fuzz-syscall-args`.

**In QEMU:**

- `make wow-demo` — boot, let the counter advance, power off mid-run, boot
  again, assert the first value printed after reboot is either `last_printed`
  or `last_printed + 1`, and nothing else. Run over N cycles like
  `make crash-test`.

  Both outcomes are correct and the reason is worth stating, because a test
  asserting a single value will fail spuriously forever. The component prints
  inside the handler; the commit happens after the handler returns. A cut in
  that window loses the printed tick, so resume repeats it — `last_printed`. A
  cut after the commit keeps it, so resume advances — `last_printed + 1`. Any
  other value is a real bug: lower means a committed tick was lost, higher
  means a tick was double-counted.
- Negative control: wipe the disk, boot, assert the counter starts at zero and
  the resume line does not appear. Without this, a demo that hardcodes its
  start value passes.

The leak test still owed from slice 1 belongs here, since this slice
instantiates components repeatedly: instantiate, tear down, instantiate again,
and assert `kheap_used_bytes` did not move the second time. The obvious
before/after comparison cannot work — it is a high-water mark that never
decreases.

## Who writes what

**Jacob writes** — silent failure, judgment, permanent:

| Piece | Why |
|---|---|
| The instance-state and root on-disk layouts | Permanent formats |
| The syscall ABI as implemented | Permanent, and the first syscall sets house style |
| The resume path (U20) | Silent failure; a wrong resume is a wrong demo |
| The tick loop and commit sequencing | The durability contract itself |
| Registry format and ID allocation | Permanent format |

**Jacob writes** — loud failure, mechanical, spec-driven:

| Piece | Why |
|---|---|
| `tools/hosted/test_write_ordering.c` | Crash-injection rig. Delivered |
| The counter component (Zig → `wasm32-freestanding`) | Tooling, as in slice 1 |
| `kernel/wasm/syscall_args.zig` + tests + fuzzer | Zig trust-boundary validator |
| Component-state serialization tests | Test harness |
| `make wow-demo` harness and build integration | Build system |
| Spec, plan, devlog, project documentation updates | Docs |

Jacob reviews everything, including Jacob's pieces.

## Risks

1. **The syscall surface is a guess.** Twelve calls declared before anything but
   `jani_log` has run. Accepted per D3.6; slice 3's IDL pass is where a wrong
   guess gets corrected, and that correction is a break if it lands after
   components exist.
2. **Flush on real hardware.** Still assumed. D3.5 narrows it to one claim, and
   only real metal settles it. Every crash-safety property depends on it.
3. **Linear memory size.** A 64 KB copy per tick is fine at 1 Hz and is not fine
   at 1 kHz. D3.10 records the upgrade path; Phase 4 metering decides when.
4. **Kernel stack depth in the resume path.** WAMR's loader recurses on the
   native stack, and resume runs it during early boot. Measure available stack
   on the resume path specifically, not just the first-boot path.
