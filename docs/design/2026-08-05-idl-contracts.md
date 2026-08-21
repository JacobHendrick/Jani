# IDL v1 and the contract toolchain — design

Phase 3, slice 3 of 4. Slice 2 landed on `main` on 2026-08-05.

## Goal

Make the kernel/component contract exist in exactly one place.

The twelve syscalls are currently written by hand twice: as host wrappers and a
`NativeSymbol` table in `kernel/wasm/syscalls.c`, and as `extern "env"`
declarations in `components/counter/counter.zig`. Nothing compares the two.
WAMR trusts the signature strings, so a disagreement is not a link error or a
crash — the guest receives whichever bytes the host pushed, interprets them
under the wrong types, and the damage surfaces later with no connection to its
cause.

Slice 3 replaces the second hand-written copy with a generated one, and makes
disagreement a compile error rather than a silent runtime fault.

## Success criteria

1. `idl/syscalls.idl` and `idl/records.idl` are the only hand-written
   description of the syscall surface and of the four on-disk record layouts.
2. `components/counter/counter.zig` declares no `extern "env"` functions. It
   imports the generated `sdk/zig` instead, and `make wow-demo` still passes
   3/3 cycles.
3. `kernel/wasm/syscalls.c` contains no hand-written `NativeSymbol` array. It
   includes a generated table, and every `_impl` body is unchanged apart from
   the D3.11 signature revision.
4. A wrong `.idl` fails the build, not the demo. `make idl-negative` seeds
   three defects and requires all three to be rejected.
5. `make test` and `make kernel` stay green; `tools/idlc`'s own tests join
   `make test` under ASan/UBSan.

## Non-goals

- **Component interfaces (typed message sets).** The blueprint's IDL eventually
  describes these for hot-swap compatibility (U3). No consumer exists until
  Phase 4. Deferred.
- **CRDT merge discipline per type.** Phase 5. Deferred.
- **Intent-provider manifests.** Phase 8. Deferred.
- **Schemas as objects in the store.** The blueprint's self-describing-types
  property requires schema objects that type IDs point at. That is a store
  change, not a toolchain change, and nothing reads schemas at runtime yet.
  IDL v1 is a build-time contract only.
- **Fuzzing `idlc`.** It parses a trusted, in-repo, build-time file. It is not
  a trust boundary and does not earn a campaign.
- **Generating host wrapper bodies.** See D3.12.

## Decisions

### D3.11 — Four arguments become unsigned, and nothing else about the ABI changes

D3.6 declared all twelve syscalls before much had run and named this slice as
the scheduled place to correct a wrong guess. Reading all twelve, the wrong
guesses are not scattered; they are one pattern repeated four times.

Four arguments are declared signed and then rejected at runtime for being
negative:

| Call | Argument | Today | Guard | Becomes |
|---|---|---|---|---|
| `object_create` | `size` | `i32` | `size < 0` (`syscalls.c:99`) | `u32` |
| `object_read` | `offset` | `i32` | `offset < 0` (`syscalls.c:159`) | `u32` |
| `object_write` | `offset` | `i32` | `offset < 0` (`syscalls.c:206`) | `u32` |
| `timer_set` | `delay_ticks` | `i64` | `< 0` (`syscalls.c:376`) | `u64` |

None of the four can meaningfully be negative. Each guard exists only to undo
a declaration that was wrong.

For the three object calls the guard is simply dead once the argument is
unsigned, because an upper bound already exists behind it: `object_create`
falls through to `> SYSCALL_TRANSFER_MAX`, `object_read` to
`jani_syscall_clamp_read`, and `object_write` to `jani_syscall_check_span`. A
hostile `0xFFFFFFFF` still returns an error, from the bound that should have
been rejecting it all along.

**`timer_set` is the exception, and it needs a new guard rather than a deleted
one.** It has no upper bound behind the sign check — `delay_ticks < 0` is the
only thing constraining it. Today a non-negative `i64` keeps
`logical_time + delay_ticks` far from wrapping. As a bare `u64`, a delay near
`UINT64_MAX` wraps the addition at `syscalls.c:380` and produces a deadline in
the past, so an absurdly distant timer silently becomes an immediate one. This
is a behavioural defect, not a memory-safety one, but it is introduced *by* the
revision and must land with it: `timer_set` becomes `u64` **and** gains a
checked deadline computation that returns `JANI_EINVAL` on overflow.

That asymmetry is the argument for making this change deliberately rather than
mechanically. Three of the four are pure subtraction; the fourth is not.

**The WAMR signature strings do not change.** A WASM `i32` is a 32-bit slot
regardless of how either side interprets its sign, so this is a guest-side and
validation-side change with no effect on the host calling convention. That is
what makes it cheap today. It stops being cheap once more than one component
exists, which is the whole argument for taking it now.

Two further warts are recorded and **deliberately not changed**:

- `message_recv` treats `capability_out == 0` as "no output pointer"
  (`syscalls.c:358`). Linear address 0 is legal in WASM, so this is formally
  ambiguous. Zig's `?*i32` lowers null to exactly this and places nothing at 0.
  Removing the ambiguity costs either a mandatory out-parameter or a second
  syscall, for no reachable safety gain.
- `time_logical` returns `i64` carrying negative errnos, while logical time is
  `u64`. At the current 1 Hz tick the ranges collide in ~292 billion years.

One thing that resembles an ABI defect and is not: `message_send` rejects any
target that is not self (`syscalls.c:309`). That is a slice-2 delivery
limitation. The signature is already correct for multi-component delivery and
does not change.

### D3.12 — `idlc` generates the symbol table and the guest SDK, never the wrapper bodies

The host side of `syscalls.c` is two kinds of code sharing a file.

The `NativeSymbol` array (`syscalls.c:424-437`) is pure transcription — names
and WAMR signature strings — and it is **the only place the host encodes
argument types**. It is exactly the surface where a mistake is silent.

The `_impl` bodies are trust-boundary judgment: capability rights checks,
object ID derivation, bounds clamping, `ENOSPC` paths. Per the working
agreement these are Jacob's, and they are green and fully tested.

`idlc` therefore emits `kernel/wasm/generated/syscall_table.h`, which
`syscalls.c` includes, and emits nothing else on the host. A syscall present in
the IDL with no matching `_impl` becomes a compile error; a signature string
can no longer drift from the guest declaration because both are printed from
one source.

Generating the wrapper bodies was considered and rejected. It would place
generated code directly on the trust boundary and rewrite tested code, trading
a silent-drift class we can close cheaply for a code-generation class we
cannot.

### D3.13 — Records are described and asserted, never regenerated

The four existing on-disk formats — `component_registry_header`,
`component_root_record`, `component_captable_header`, and
`instance_state_header` — stay hand-written and authoritative in
`component.h` and `instance_state.h`.

`idlc` emits `kernel/wasm/generated/records_conform.h`: a `_Static_assert` per
field offset and per total size. A `.idl` that drifts from a struct, or a
struct whose field moves, fails to compile.

These are permanent on-disk formats. Regenerating them would convert a
code-generation bug into a disk-corruption bug — the class the whole project
is built to avoid — in exchange for removing a duplication that static asserts
already make harmless.

New component-facing record types (the counter's state) are not yet on disk in
a fixed layout and **do** get generated guest accessors. That is where the
payload law (object IDs and offsets, never language pointers) starts being
enforced by construction rather than by discipline.

### D3.14 — The type vocabulary is bounded by what the Zig validators can check

`kernel/wasm/syscall_args.zig` validates argument *kinds*, not individual
syscalls. IDL v1's types map onto it one to one:

| IDL type | Validator |
|---|---|
| `slice<u8>` | `jani_syscall_check_span` |
| `cap` | `jani_syscall_check_slot` |
| `cap?` | `jani_syscall_check_optional_slot` |
| object span (offset + length) | `jani_syscall_check_transfer` |
| `u32`, `u64`, `i32`, `i64` | none needed |
| `type-id` | none; lowering rule only |

**A type with no validator behind it does not enter the grammar.** The IDL is a
permanent format; this rule prevents it growing syntax the trust boundary
cannot enforce, which is the failure that would be expensive to reverse.

`type-id` is the one type needing a declared lowering rule: one logical 128-bit
value carried as two `i64` parameters, because WASM has no `i128`. The rule is
stated once in the IDL rather than re-derived at each call site.

### D3.15 — `idlc` is C, and its emitters are separable

`idlc` is written in C, matching `tools/hosted/`. Its parser tests join
`make test` under the existing ASan/UBSan gate with no new build machinery and
no second toolchain in `tools/`.

Its four emitters — Zig SDK, C SDK, syscall table, record conformance — are
separated behind one internal interface. This costs nothing now and is what
makes a later third language an addition rather than a rewrite.

## Structure

```
idl/syscalls.idl          Jacob    the twelve calls, post-D3.11
idl/records.idl           Jacob    the four on-disk layouts

tools/idlc/               Support   lexer -> parser -> AST -> emitters
tools/hosted/test_idlc.c  Support   parser and emitter unit tests

sdk/zig/jani.zig          generated
sdk/c/jani.h              generated
kernel/wasm/generated/syscall_table.h     generated
kernel/wasm/generated/records_conform.h   generated
```

Generated files are committed, not built on demand. The kernel build must not
depend on `idlc` having run, and a reviewer should see generated output change
in the diff when an `.idl` changes.

## Data flow

`.idl` files are read once at build time by `idlc`, which writes four outputs.
`syscalls.c` includes two of them; `counter.zig` imports one. Nothing reads
IDL at runtime in this slice.

Build order: `idlc` runs before `make kernel` and before the component build.
A stale generated file is caught by `make idl-check`, which regenerates into a
temporary directory and diffs — so a committed generated file that no longer
matches its `.idl` fails CI rather than shipping.

## Verification

| Gate | What it proves |
|---|---|
| `make test` | `idlc`'s lexer, parser, and emitters, under ASan/UBSan |
| `make kernel` | the generated table compiles against the real `_impl` set |
| `make idl-check` | committed generated files match their `.idl` sources |
| `make idl-negative` | a wrong `.idl` is rejected, not silently accepted |
| `make wow-demo` | the generated Zig SDK actually works, end to end |

`make idl-negative` seeds three defects, each behind its own flag, mirroring
`model-check-negative` and `wow-demo-negative`:

1. A syscall argument retyped (`u32` → `i64`) — must fail the build.
2. A record field moved — must fail the `_Static_assert`.
3. A syscall deleted from the IDL — must fail to link against its `_impl`.

A gate nobody has watched fail is not yet a gate. This is the same reasoning
that made `CACHE_MODE=unsafe` passing worth taking seriously.

The strongest single check is criterion 2: `counter.zig` losing its `extern`
block means the generated SDK is load-bearing for a demo that already has a
passing gate and a negative gate behind it.

## Ownership

The grammar, both `.idl` files, `tools/idlc`, its tests, the emitters, the
negative gate, and the build wiring are maintained together. Review covers
the grammar and generated output because both form part of the ABI.

## Risks

**The D3.11 revision is an ABI change made to reduce risk, which is itself a
risk.** It touches `syscalls.c`, `counter.zig`, and `test_syscall_args.c` in
the same slice that introduces a new toolchain. A failure could originate in
either. Mitigation: land D3.11 as its own commit with every gate green before
any `idlc` code exists, so the two are bisectable.

**Scope.** The slice-3 estimate was ~800 lines. Syscalls plus records plus four
emitters plus the negative gate is closer to 1,400–1,800. The seam if it must
split: syscalls and the SDK first (criteria 1–3), records and conformance
second (D3.13). D3.15's separable emitters are what make that split free.

**Reserved-but-unexercised design.** IDL v1 does not cover component
interfaces, and Phase 4 hot-swap will need them. The grammar should not be
designed as though syscalls and records are all there will ever be, but neither
should interface syntax be invented before its consumer exists. This tension is
accepted rather than resolved.

**Unchanged from slice 2, and still the sharpest unknown in the project:**
whether the physical device honors a flush. Only real power loss on real
hardware settles it.
