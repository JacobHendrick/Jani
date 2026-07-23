# Jani OS

A from-scratch, next-generation operating system built around five pillars:

1. **Orthogonal persistence** — no files, no "save"; all state lives in a
   versioned object store and survives power-off. Running programs resume
   exactly where they were.
2. **Zero-trust hardware** — everything encrypted at rest and in flight,
   measured boot, designed for confidential-computing hardware.
3. **Distributed compute** — many devices, one computer; running programs
   migrate between machines mid-execution.
4. **Semantic intelligence** — the OS indexes what data *means*; queries by
   similarity, not by path.
5. **Intent-based UI** — no apps; disposable micro-runtimes composed on one
   canvas to fulfill what the user asks for.

Plus eighteen upgrades (time-travel snapshots, capability security,
sandboxed drivers, provenance ledger, live collaboration, hot-swap
components, and more) riding on three substrates: the persistent object
store, capabilities, and WASM components.

## Languages

- **C** — small privileged kernel trunk (built with `zig cc`)
- **Zig** — kernel trust boundaries and the primary language for system
  services, desktop/UI components, applications, and the official SDK
- **WASM** — the ABI for everything above the kernel (WAMR, interpreter mode)
- ~300 lines of x86_64 assembly, total

Zig expands with the project: it is the C toolchain in Phase 0, begins
providing native safety-boundary modules in Phase 2, targets WASM for services
and applications from Phase 3 onward, and implements the compositor, widget
renderer, and intent-driven desktop in Phase 8. No Odin toolchain or Odin SDK
is planned; components previously assigned to Odin will be written in Zig.

## Where everything is

- **`jani-os-blueprint.txt`** — the complete blueprint: architecture, all
  eleven phases, milestones, reading lists. This is the map for the whole
  project. Read Part 2 (architecture) and Part 4 (C discipline) before
  writing anything.
- **`START-HERE.txt`** — the concrete first-session checklist for Phase 0.
- **`docs/devlog.md`** — the development log. One entry per session; your
  future self debugging Phase 5 will thank present you.
- Directory layout follows Part 7 of the blueprint and grows one phase at a
  time. Phase 0's boot, architecture, driver, and kernel-library code is live.

## Verification Commands

- `make test` — run hosted PMM and heap tests under ASan/UBSan
- `make fuzz-heap` — run the bounded randomized heap fuzz campaign
- `make model-check` — prove the current WAL model satisfies its invariants
- `make model-check-negative` — prove TLC breaks all seeded protocol mutants

## Status

- [x] Phase 0 — bare-metal on-ramp (boot, print, interrupts)
- [x] Phase 1 — memory & the single address space
- [ ] Phase 2 — persistent object store
- [ ] Phase 3 — WASM runtime as userspace  ← the "wow" demo lives here
- [ ] Phase 4 — capabilities, scheduler, replay, hot-swap
- [ ] Phase 5 — distribution
- [ ] Phase 6 — semantic layer
- [ ] Phase 7 — zero-trust hardening
- [ ] Phase 8 — intent UI
- [ ] Phase 9 — Linux ABI & the app ecosystem  ← Chromium runs here
- [ ] Phase 10 — real hardware & real users  ← the consumer milestone
