# Consumer Viability Design

**Date:** 2026-07-22
**Status:** Approved design, pending implementation plan
**Amends:** `jani-os-blueprint.txt` Parts 1, 5, 6

## Goal

Make Jani a system real people run on real desktops and laptops, without
giving up the persistence, capability, and semantic architecture that make it
worth building. Consumer viability is a project goal, not an aspiration.

## Fixed constraints

These were settled during design and are not open for renegotiation:

1. **Everything is written from scratch.** No borrowed kernel, no Linux
   underneath, no compatibility shim over someone else's OS. The one exception
   is large, well-bounded, security-reviewed libraries brought in deliberately
   — the same bargain already made for WAMR, crypto, and ML-KEM.
2. **Target is x86_64 desktops and laptops.** Not an appliance, not a VM-only
   research target, not ARM.
3. **The app model is a Jani-native capability ABI with a WASI shim on top.**
4. **Sequencing:** complete the novel stack in QEMU (Phases 3-8), then bring up
   real hardware. The consumer milestone is an ISO a stranger can boot on
   their own PC.

## Decisions

### D1: Hardware-honest QEMU (Approach A)

The blueprint phase order is unchanged. One thing changes: starting at Phase 4,
QEMU is invoked with emulated *real* device models rather than virtio alone.

```
-device nvme,drive=d0     real NVMe spec
-device qemu-xhci         real USB 3
-device e1000e            real Intel NIC
-device ich9-ahci         SATA
```

virtio remains the fast path for hosted tests and quick iteration; it stops
being the only backend.

**Rationale.** The chosen sequencing (novel stack first, metal later) risks a
cliff: six phases of comfortable virtio development, then a multi-year driver
project during which architectural assumptions meet hardware for the first
time. QEMU emulates the actual specs, so real drivers can be written with a
debugger attached. The final bring-up shrinks from "write every driver" to
"debug the drivers you have."

The seam already exists. `struct object_store_io` (kernel/obj/object_store.h)
is function pointers over `read_sector`/`write_sector`/`flush`; virtio-blk,
NVMe, AHCI, and the hosted test's `disk_write` all sit behind it unchanged.

### D2: Two-tier driver architecture

U5 (sandboxed drivers) collides with a bootstrap problem: the WASM runtime
arrives in Phase 3, but the object store must read from disk before that.

| Tier | Scope | Location | Restartable |
|---|---|---|---|
| **Boot driver** | Minimal, read-mostly; enough to mount the store and load the runtime. NVMe + AHCI only. | In-kernel C, permanent | No |
| **Full driver** | Complete functionality, error recovery, hotplug, power management. | WASM component | Yes (U5) |

Boot drivers stay deliberately small — a few hundred lines per device, no
hotplug, no async, no recovery — because they cannot restart and must be
auditable line by line.

Sequencing under D1: write the in-kernel NVMe boot driver at Phase 4 against
`-device nvme`, then rehost the full driver as a component once the Phase 3
runtime exists.

### D3: IOMMU is a hard requirement

A WASM sandbox constrains what driver *code* touches. It does not constrain
what the *device* touches. Drivers hand devices physical addresses for DMA;
the device performs the write, not the CPU. A driver inside a perfect sandbox
can still program its device to DMA over kernel memory.

Containing this requires an IOMMU (Intel VT-d / AMD-Vi). With it, U5 is real:
the driver gets a device, the device gets a page window, a crash is contained.
Without it, U5 is aspirational and a driver bug is a system compromise.

Consequences:

- IOMMU driver added to the Phase 4 driver set (~1.5-2.5k lines).
- "IOMMU enabled in firmware" becomes a hardware compatibility requirement.
- The installer must **detect it and say so plainly**. VT-d/AMD-Vi is present
  on essentially all x86_64 silicon from ~2015 but is frequently disabled by
  default. With it off, a headline security property silently is not there,
  and the system must not pretend otherwise.

### D4: Native ABI with a WASI shim

The Jani-native capability ABI is the real interface. A WASI compatibility
layer sits on top of it.

WASI preview 2 is built on handles and resources — a capability model already
— so the capability engine *is* the WASI implementation rather than a
translation layer beside it:

| WASI concept | Jani |
|---|---|
| Preopened directory handle | Capability grant |
| Resource handle | Capability handle |
| `fd_read` on a handle | Read through the capability |
| No handle for it | Not expressible (matches WASI's own model) |

A WASI binary that respects the sandbox runs unmodified. One that assumes an
ambient filesystem gets nothing — correct behavior, not a bug. Anything
compiled from Rust, Go, C, C++, or Zig to WASI is a candidate app.

Estimated ~2-4k lines, mostly mapping rather than mechanism.

### D5: Powerbox authority granting — no permission dialogs

Applications receive **zero authority by default**. Authority is granted by
the act of selection: the user picks an object, and that picking hands the
component a capability to exactly that object. Nothing broader exists to grant.

This is the powerbox pattern from capability-security research (CapDesk,
Polaris). Prior attempts failed because they were retrofits: as long as a path
namespace exists and applications can `open()` arbitrary strings, some
application demands broad access and users click through. Jani has no path
namespace — a component that has not been handed a capability cannot *name* an
object, so it cannot express the request.

The flat 128-bit ID space, adopted for persistence reasons, is the enabling
condition for the security model.

### D6: Web access is delivered by running real browsers on the Linux ABI

**Superseded during design.** The original decision was to vendor Ladybird into
a native component tier. D11 removes the need: with a Linux ABI layer,
Chromium and Firefox run unmodified, so there is no engine to port and no
native component tier to define.

Rejected alternatives, recorded so they are not revisited without cause:

- **Write our own engine.** The web platform is 1,000+ specifications. Ladybird
  has a funded full-time team and remains pre-alpha after roughly six years;
  Servo had Mozilla engineers from 2012 and never shipped a complete browser;
  V8 and SpiderMonkey are each ~1M lines of JavaScript alone. Not solo-reachable.
- **Port Chromium directly onto Jani-native interfaces.** Chromium plus
  dependencies is tens of millions of lines — 400-800x the size of the rest of
  this OS — and assumes POSIX threads, mmap semantics, a real filesystem, BSD
  sockets, Mojo IPC, GPU compositing, FreeType/Fontconfig/HarfBuzz, and
  seccomp/namespace sandboxing throughout. Fuchsia ported Chromium and it took
  Google, owning both projects, years. Its sandbox would also need rewriting
  onto capabilities, and a 4-week security-release cadence makes rebasing
  permanent work. Porting it buys exactly one application.
- **Vendor Ladybird or WPE WebKit.** Viable, but strictly worse than D11 once
  the ABI layer exists: both deliver one browser, where D11 delivers the whole
  Linux application ecosystem for comparable effort.

The security claim survives the change. A browser's own process split
(renderer, GPU process, network service) becomes a capability split inside the
Linux personality: a renderer exploit cannot escalate because the renderer
holds no capability to escalate with.

### D11: Linux ABI compatibility component

Jani implements the **Linux x86_64 syscall interface** — syscall number in
`RAX`, arguments in `RDI/RSI/RDX/R10/R8/R9`, result in `RAX` — plus ELF binary
loading. Unmodified Linux binaries then run: Chromium, Firefox,
`wpa_supplicant`, and the broader ecosystem.

**This does not violate the from-scratch constraint.** An interface is not an
implementation; every line is first-party. Precedent: WSL1 (Linux syscalls on
the NT kernel), gVisor (Google, in Go), FreeBSD's Linuxulator, Fuchsia's
Starnix, and Asterinas.

**Scope.** ~350 syscalls exist, but roughly 100 carry most software. The
difficulty is semantics rather than count: exact error codes, signal
interaction with blocked calls, `fork`/`exec` corner cases, `mmap` overlap
rules, `/proc` contents, `futex`, and `epoll` edge-vs-level triggering. Much of
it is undocumented behavior that real programs depend on. Estimated
**30-60k lines**; gVisor is ~200k lines of Go for a fairly complete
implementation.

**Reconciling with D5.** The Linux ABI assumes paths, uid/gid, file
descriptors, and ambient authority — exactly what was deleted to make the
powerbox work. Resolution: the Linux personality is **itself a component
holding capabilities**. Inside it, processes see a normal POSIX world with a
synthetic filesystem; from outside, the entire world holds only the
capabilities it was handed. Ambient authority exists, but ambient *within a
scoped box*.

This is the U13 ephemeral-world primitive generalized: legacy software gets the
ambient authority it demands, inside a world whose total authority is bounded.
Fuchsia demonstrates the composition — Starnix processes are ordinary
capability-system components, so Linux compatibility did not cost Fuchsia its
capability model.

**Three app tiers result:** native WASM components (D4), WASI binaries (D4),
and Linux binaries (D11). Native components remain the intended way to write
*for* Jani; the Linux tier is how Jani inherits software that already exists.

### D12: Wifi

Required for the consumer milestone — a laptop OS without wifi is not one.

**Division of labor**, following Linux's split:

| Half | Responsibility | Owner |
|---|---|---|
| Kernel | Chip driver + 802.11 MAC (scan, associate, encrypt frames) | Jani |
| Userspace | WPA2/WPA3 handshake, key management, network selection | `wpa_supplicant`, unmodified via D11 |

**D11 pays for itself here.** `wpa_supplicant` is userspace software speaking
nl80211 netlink to the kernel. With the ABI layer plus an nl80211-shaped
interface, the real `wpa_supplicant` runs — meaning the WPA 4-way handshake,
SAE exchange, and key derivation are never hand-written. That is the correct
~8-10k lines to skip: hand-rolled WPA crypto is precisely where a subtle bug
destroys the security property while everything appears to work.

**Chip strategy, two stages:**

*Stage 1 — USB dongle.* One driver serving every machine, desktop or laptop,
riding the XHCI stack already built for keyboards. Target **ath9k_htc**
(Atheros AR9271/AR7010) — the only wifi hardware with genuinely open-source
firmware, so no vendor blob and no redistribution licensing question. It is
802.11n and mostly 2.4GHz, therefore slow by current standards, but it works
and unblocks the consumer milestone.

*Stage 2 — built-in PCIe.* **Intel AX200/AX210** (iwlwifi), covering the
majority of laptops built since 2019. Requires a vendor firmware blob, raising
a distribution question Linux answers by shipping `linux-firmware` under
redistribution terms; Jani would do the same.

| Piece | Est. lines |
|---|---|
| 802.11 MAC layer (mac80211-equivalent) | 8-12k |
| nl80211 interface for `wpa_supplicant` | 2-3k |
| ath9k_htc USB driver (Stage 1) | 5-8k |
| Intel AX200 driver (Stage 2) | 10-15k |
| **Total** | **25-38k** |

Initial subset: WPA2-PSK only — no enterprise EAP, no mesh, no AP mode, no
power management. WPA3/SAE follows once the stack is stable.

### D7: Malware cannot persist

Orthogonal persistence removes a property conventional systems get free:
rebooting forgets things. It must be engineered back in deliberately.

**Three tiers of state, with different persistence rules:**

| Tier | Persistence | Why malware cannot live there |
|---|---|---|
| **System** | Declaration only (U17), rebuilt | Anything absent from the signed declaration does not survive a rebuild |
| **Component code** | Signed, immutable objects | Minting an executable object requires a capability malware does not hold |
| **User data** | Fully persistent (Phantom-style) | Data is not executable and carries no capabilities |

Phantom-style persistence applies fully to user data, which is what users care
about. The system layer is not persisted as mutable state; it is persisted as a
*description* and reconstructed.

**Executability is a capability, not a bit.** On conventional systems, "is this
executable" is a permission bit anything can set — the root of most persistence
techniques. Here, an executable component is an object of a specific `type_id`,
and only the component-manager capability can mint one. Every component object
is signed by a developer keypair (U8); the signature is verified before
execution. This is W^X raised from memory pages to the object level.

**Code does not persist; the signed object does.** A component resumes from its
signed immutable code object plus its saved data state. On resume, code is
re-verified from the signature and reloaded fresh. Injected code, patched
functions, and hooked pointers are not in the signed object and do not return.
Only data resumes, and data holds no authority.

This inverts the usual relationship: on conventional systems reboot clears
memory-resident malware but leaves disk-resident persistence intact. On Jani,
memory state persists (the feature) but the code path is reconstructed from
signed objects every time, so memory-resident compromise cannot survive, and
disk-resident compromise cannot become executable.

Neither half works alone: signing without declarative rebuild leaves a mutable
system layer to hide in; declarative rebuild without capability-gated
executability lets malware re-mint itself. U17 and U8 therefore move from
"later-phase" to load-bearing.

**Residual risk (bounded, not eliminated):** a user may grant a capability to a
malicious component. Mitigations: blast radius is exactly what was granted (no
ambient authority, no lateral movement); revocation is real (U6) and renders
the component inert; the provenance ledger scopes cleanup by recording what was
touched; untrusted code runs in an ephemeral world (U13) and is discarded with
zero residue; rollback to before the grant (U1) with provenance identifying
when that was.

### D8: Out-of-band recovery

Orthogonal persistence breaks "turn it off and on again." This is the highest-
risk consumer failure mode in the design and contributed to Phantom OS's
usability problems.

Requirements:

- **Automatic snapshots before every risky operation** — system update, app
  install, driver change. Not user-initiated; users do not snapshot in time.
- **A bootloader recovery entry** that boots directly to a chosen snapshot,
  before the store is fully mounted.
- **Recovery must not depend on the running system.** If the object table is
  corrupt, recovery cannot be a component that needs the object table to load.
- **Snapshot retention policy** guaranteeing a reachable known-good state.

**Constraint on Phase 2 code not yet written:** snapshot rollback's on-disk
representation must be simple enough for a few hundred lines of boot-time C to
execute without the component system, the WASM runtime, or a mounted table. If
snapshots require the full store to interpret, the result is a recovery system
that only works when it is not needed. This must be settled now, while
snapshots are four functions rather than a shipped format.

### D9: Dual shell — conventional launcher alongside intent UI

Phase 8's intent UI ships with a conventional launcher beside it. Components
appear as recognizable, findable things; intent composition is available for
users who want it.

**Rationale.** "No apps, state your intent" is the most novel element of the
blueprint and its largest consumer risk. The Rabbit R1 and Humane AI Pin both
shipped that model in 2024 and both failed hard with consumers, who turned out
to want predictability and discoverability. Shipping both lets users arrive
somewhere familiar and grow into the new model rather than facing a blank
canvas on first boot. This is a shell decision, revisitable at Phase 8.

### D10: Installer, pre-flight, and hardware compatibility

The installer is a first-impression deliverable, not an afterthought (~2-4k
lines). It must:

1. Boot from USB and **probe before committing** — CPU, IOMMU present *and
   enabled*, UEFI mode, storage controller, network chip.
2. Report findings honestly *before touching the disk*, e.g. "Your wifi chip
   is unsupported; ethernet or a USB dongle will work. Continue?"
3. Call `object_store_format`.
4. Generate the user identity keypair (U8).
5. Install the bootloader and the D8 recovery entry.

A **pre-flight checker running on Windows or Linux** lets a user learn whether
their PC is supported before burning an ISO.

Published hardware compatibility list with three tiers: **Verified** (owned and
tested), **Expected** (standards-only hardware), **Unsupported** (requires
absent wifi/GPU drivers).

## Driver scope

| Driver | Covers | Est. lines |
|---|---|---|
| NVMe | every modern SSD | ~1k |
| XHCI + USB HID | keyboards, mice, storage, dongles | ~4k |
| PCIe enumeration | device discovery | ~500 |
| e1000e + RTL8169 | most desktop ethernet | ~2.5k |
| AHCI | SATA | ~1k |
| ACPI subset | shutdown, reboot, IRQ routing | ~2k |
| IOMMU (VT-d / AMD-Vi) | D3 | ~1.5-2.5k |
| UEFI framebuffer | display, no acceleration | mostly done via Limine |
| **Total** | | **~14-21k** |

**Deliberately out of scope initially.** Wifi has no standard; every chip needs
vendor firmware blobs and Linux's iwlwifi alone is ~50k lines. GPU acceleration
has no standard and is enormous. Near-term answers: USB ethernet or a single
supported chip for networking; software compositing on a plain framebuffer,
which a modern CPU handles at 1080p. Consequences — no 3D, no hardware video
decode — belong in the HCL, stated plainly.

## Scope impact

| Addition | Est. lines (yours) | Phase |
|---|---|---|
| Drivers (incl. IOMMU) | 14-21k | 4 (written) / 10 (hardened) |
| Installer, pre-flight, HCL, recovery | 2-4k | 10 |
| WASI shim | 2-4k | 3 |
| Signing, declarative system state, resume verification (D7) | 2-3k | 4 |
| Linux ABI compatibility (D11) | 30-60k | 9 |
| Wifi (D12) | 25-38k | 10 |
| **Total added** | **~75-130k** | |

Against the blueprint's original 25-50k, the consumer target is roughly
**100-180k lines of first-party code.**

Calibration, since the number matters: Linux 0.01 was ~10k lines, Linux 1.0
(1994) ~176k, Linux 2.0 (1996) ~750k, and Linux today ~30M. This target sits
between Linux 1.0 and 2.0 — a from-scratch kernel of proven, historically
achieved scale, reachable solo over years. It is emphatically not "Linux size"
in the modern sense; most of Linux's 30M lines are drivers for hardware Jani
will never support, plus a dozen CPU architectures and sixty filesystems.

Blueprint Part 6 carries the same figures; keep them in sync.

## Failure behavior

- A driver component crashes → capabilities revoked, IOMMU unmaps its DMA
  window, the component restarts from its object, in-flight requests fail
  cleanly to callers.
- The object store never observes a partial write: a driver death is exactly
  the crash case `docs/models/WalCommit.tla` already covers. The Phase 2
  crash-consistency work is what makes restartable drivers safe in Phase 4 —
  the same guarantee.
- Boot drivers cannot restart, hence D2's size limit.

## Testing strategy

- Hosted-first (R3) is unchanged and remains primary.
- Drivers gain a QEMU integration tier under D1: each driver has a boot-and-
  exercise test against its emulated real device.
- D7 requires negative tests: an unsigned component must fail to execute; a
  component without the component-manager capability must fail to mint an
  executable object; a resumed component must reload code from its signed
  object rather than from persisted memory.
- D8 requires a recovery test that corrupts the object table and verifies
  bootloader-level rollback succeeds without a mounted store.

## Open questions

1. **Linux ABI subset boundary.** Which ~100 syscalls constitute the initial
   target, and what the failure mode is for an unimplemented one (`ENOSYS`
   versus a hard error with diagnostics). Unresolved until Phase 9 begins.
2. **Does D11 weaken D7?** Linux binaries are not signed Jani component
   objects, so the U20 "code re-derived from signed objects on resume"
   guarantee does not extend into the Linux personality as written. Options:
   sign the personality's root filesystem image as one object, rebuild the
   personality from a declaration on every boot (U17), or accept that the
   Linux tier has conventional persistence and confine it accordingly. **This
   is the most important unresolved question in the spec** — it is where the
   malware-non-persistence property and the app ecosystem collide.
3. **GPU for Chromium.** Chromium expects GPU compositing; software rendering
   is slow and Jani has no GPU driver. Whether a real browser is usable on a
   software framebuffer at 1080p needs measurement, not assumption.
4. **Firmware blob distribution** for Stage 2 wifi (D12) — licensing and
   packaging, following `linux-firmware`'s model.
5. **Snapshot retention policy.** How many, how long, and what evicts them.
6. **Declarative system state format (U17).** Load-bearing for D7 and possibly
   for resolving question 2.

## Next step

Snapshot on-disk format under D8's constraint is the immediate blocker: it
gates recovery design and it is Phase 2 work in progress right now. The
implementation plan should start there.
