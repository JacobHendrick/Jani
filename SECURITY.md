# Security Policy

Jani is under active development. Current builds are suitable for testing in
QEMU with non-sensitive data. They are not yet suitable for production use,
secrets, or hostile workloads.

## Reporting a Problem

Use GitHub's private vulnerability reporting feature when it is enabled for
this repository. If private reporting is unavailable, open an issue that asks
the maintainer for a private contact channel. Do not include exploit details
in that issue.

Ordinary correctness bugs can be reported in the public issue tracker.

## Current Trust Model

The kernel and the WAMR interpreter run in x86 Ring 0. WASM linear-memory
bounds checks and Jani syscall validation are the current component isolation
boundary. A memory-safety bug in the kernel or WAMR may therefore compromise
the whole system.

Do not run untrusted WASM modules yet.

Current defensive controls include:

- WAMR classic interpreter mode with JIT, AOT, fast interpreter, WASI,
  threads, and shared memory disabled
- a finite instruction budget for each component entry
- validation of guest memory ranges before syscall reads and writes
- a 4096-byte limit on object and message transfers
- capability checks for object and message operations
- generation-checked persisted lineage and transitive remote revocation
- handler-wide atomic persistence and rollback on failed publication
- checked DMA offsets and non-delegable driver/supervisor entry points
- W^X enforcement for pages created by Jani's page mapper
- CR0.WP enabled so Ring 0 respects read-only page mappings
- checksummed persistent records and write-ahead-log recovery
- pinned WAMR source version and SHA-256 verification

Driver protocol execution now runs in a WASM component, but PCI/MMIO and generic
VirtIO queue transport remain in Ring 0. A timed-out queue is disabled because
completion may be uncertain. The tested supervisor restart occurs between
requests; it is not proof of safe recovery from arbitrary in-flight DMA faults.

Scheduler cycle reservations are cooperative admission budgets, not hard
real-time guarantees. Replay and provenance are bounded diagnostic records,
not authenticated audit logs. Hot-swap compatibility metadata is supplied by
trusted installation code; schema agreement does not prove semantic safety.

## Protections Not Yet Implemented

Jani does not currently provide:

- Ring 3 processes or hardware-enforced separation between components
- separate page tables for each component
- SMEP, SMAP, KPTI, or an IOMMU policy
- KASLR
- UEFI Secure Boot or measured boot
- encryption at rest or in transit
- a security response SLA

Some CPU protections depend on a future Ring 3 design. Secure Boot and
cryptographic storage also require a key-management design before they can be
claimed as security features.

The limits above are part of the public threat model. Changes that move a
trust boundary should include focused negative tests and an update to this
file.
