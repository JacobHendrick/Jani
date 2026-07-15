# Jani OS development log

One entry per session. What I did, what broke, what I learned, what's next.
Future-me debugging Phase 5 will need to know why Phase 2-me made these
choices — write them down.

---

## 2026-07-05 — Project start

Blueprint finalized (see `../jani-os-blueprint.txt`). Languages locked:
C kernel trunk built with `zig cc`, Zig at trust boundaries + services +
apps, Odin for GUI visuals, WASM/WAMR above the kernel. This original Odin
choice was superseded by the 2026-07-11 decision below. Project skeleton
created. Next session: START-HERE.txt step 1 — install tools, boot the
Limine template, hello over serial.

## 2026-07-11 — Phase 0 complete and Zig scope expanded

Phase 0 is green: Limine boot, serial `printk`, freestanding runtime, GDT,
IDT, exception reports, PIC/PIT timer interrupts, PS/2 keyboard scancodes,
and the deliberate page-fault test all work in QEMU. Language decision:
Zig is now the only first-class language above the C kernel. All desktop,
GUI, service, and application components previously planned for Odin will
be Zig components targeting WASM. Zig will expand from the Phase 0 C
toolchain into native trust-boundary modules, the component SDK, system
services, applications, and the Phase 8 desktop.

## 2026-07-15 — Phase 2 begins: WAL protocol TLA+ model (in progress)

Phase 1 re-verified end to end before moving on: hosted suites (32 PMM + 63
heap checks under ASan/UBSan), a clean 10k-run fuzz smoke, and the QEMU
serial log showing every bare-metal self-test pass. Phase 1 code itself is
still uncommitted in the working tree — commit it separately; do not mix it
into model commits.

Started Phase 2 with R9: model the WAL commit/recovery protocol before
writing any store code. Design spec and step-by-step plan live in
`docs/superpowers/specs/2026-07-15-wal-tla-model-design.md` and
`docs/superpowers/plans/2026-07-15-wal-tla-model.md` (the plan carries an
execution-status section for whoever picks this up). Done so far: TLC
vendored and sha256-pinned (download rule hardened to verify-then-move
after a security review flagged the fail-open original), the abstract
atomic-commit spec green (712 states), and the sector-level concrete spec
green crash-free (7,573 states, TypeOK + HistoryMatch) with `make
model-check` wired up. Design decisions: crashes apply an arbitrary SUBSET
of the pending write cache (covers torn records and reordering in one
action), checkpoint truncation is just the superblock scan-pointer advance,
and recovery is a pure function of durable state. Two protocol rules the
modeling already surfaced: never advance the superblock past an in-flight
logged record, and always replay the WAL in slot order because the
superblock pointer can lag the homes. One TLA+ lesson: TLC refuses
record-vs-string comparisons, so "no op in flight" is an IdleOp record
with st="idle", not a "none" string.

Remaining: plan Tasks 4–7 — the Crash action and NoTornVersionVisible
invariant (Task 4, intended as a hand-written exercise; full semantics in
the plan), the refinement mapping to the abstract spec plus the
DurableRecoverable invariant (Task 5), three BUGGY_* mutant configs that
TLC must be able to break plus `make model-check-negative` (Task 6), and
doc trueing-up (Task 7).

<!-- Next entry goes here -->
