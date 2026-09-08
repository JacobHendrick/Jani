# Phase 4 implementation

Baseline: `d79ef8bc`, pulled from main on 2026-09-07.

The phase is complete only when the blueprint's milestone demonstrations and
the existing regression gates pass. This checklist records implementation and
verification separately.

- [x] Persist and validate capability lineage; resolve exact generations.
- [x] Atomic delivery, local/remote revocation, uninstall, provenance.
- [x] Scheduler: runnable mailboxes/timers, priorities, interactive admission,
      per-component metering and bounded trace queries.
- [x] Record/replay: messages and syscall results; compare final memory.
- [x] Compatible service hot-swap with durable mailbox/state and rollback.
- [x] VirtIO request driver component and supervised crash/restart demonstration.
- [x] Hosted malformed-input, capacity, rollback and crash-point tests.
- [x] QEMU milestone, existing persistence/negative gates, builds and IDL checks.
- [x] Update public status and devlog to match verified behavior.

Verified on 2026-09-07: 14,160 hosted checks with ASan/UBSan, including 4,813
Phase 4 checks. Four Phase 4 negative controls reject seeded authority,
stale-mail, replay, and upgrade defects. The real-WASM milestone, three-cycle
persistence demo, module-free resume, 25 QEMU crash cuts, three resume negative
controls, IDL checks, and four 10,000-run fuzz targets pass. LeakSanitizer was
disabled in the traced test environment, not in the repository defaults.

This closes the bounded Phase 4 milestone. Generic hardware transport remains
kernel-owned; the interrupt-only endpoint and arbitrary in-flight DMA recovery
are not implemented. See the Phase 4 design record for that boundary.
