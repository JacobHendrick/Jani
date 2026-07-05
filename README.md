# aeon

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

- **C** — kernel trunk (built with `zig cc`)
- **Zig** — kernel trust boundaries, system services, apps, the official SDK
- **Odin** — GUI visual components only
- **WASM** — the ABI for everything above the kernel (WAMR, interpreter mode)
- ~300 lines of x86_64 assembly, total

## Where everything is

- **`aeon-os-blueprint.txt`** — the complete blueprint: architecture, all
  nine phases, milestones, reading lists. This is the map for the whole
  project. Read Part 2 (architecture) and Part 4 (C discipline) before
  writing anything.
- **`START-HERE.txt`** — the concrete first-session checklist for Phase 0.
- **`docs/devlog.md`** — the development log. One entry per session; your
  future self debugging Phase 5 will thank present you.
- Directory layout matches Part 7 of the blueprint; every folder is empty
  on purpose — the code is mine to write.

## Status

- [ ] Phase 0 — bare-metal on-ramp (boot, print, interrupts)
- [ ] Phase 1 — memory & the single address space
- [ ] Phase 2 — persistent object store
- [ ] Phase 3 — WASM runtime as userspace  ← the "wow" demo lives here
- [ ] Phase 4 — capabilities, scheduler, replay, hot-swap
- [ ] Phase 5 — distribution
- [ ] Phase 6 — semantic layer
- [ ] Phase 7 — zero-trust hardening
- [ ] Phase 8 — intent UI
