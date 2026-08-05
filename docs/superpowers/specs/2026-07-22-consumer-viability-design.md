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

### D13: The Linux personality has an immutable signed root

Resolves the U20 collision. The personality holds two kinds of state, and
conflating them is what created the problem:

| | Contents | Treatment |
|---|---|---|
| **Root image** (`/usr`, `/lib`, `/bin`) | the distro — code | immutable, signed, verified, read-only |
| **Writable layer** (`/home`, `/var`, `/tmp`) | user data, configs, profiles — data | persistent |

**Four rules:**

1. The root image is a signed, content-addressed, immutable Jani object,
   signature-verified at mount and mounted read-only.
2. **W^X at the personality level.** A page may be executable only if backed by
   the verified root image. Malware can write a file to `/home`; it can never
   execute it.
3. **JIT is a capability.** V8 genuinely needs runtime code generation, so a
   component may be granted "may create executable pages from anonymous
   memory." The browser renderer gets it; nothing else does. The grant lives in
   the personality's signed declaration.
4. **Autostart is declared, not discovered.** No cron, no systemd user units,
   no scanning `~/.config/autostart`. What runs at personality start comes from
   the declaration (U17). This eliminates essentially every classic Linux
   persistence vector.

Precedent: iOS enforces code signing at page level and grants
`dynamic-codesigning` to exactly one process (Safari's JIT); Android Verified
Boot pairs a read-only system partition with SELinux W^X; ChromeOS uses
dm-verity on a read-only rootfs with writable `/home`. All three arrived at
immutable-system-plus-writable-data independently, and all three had to
retrofit it onto a mutable Unix. Jani gets it by construction.

**Side effect:** distro updates become object version bumps — a new signed root
image, atomic, rollback-able via U1 using the same snapshot machinery. That is
ChromeOS's A/B update model, and strictly better than `apt`/`dnf`, where a
failed upgrade leaves a state nobody designed.

**Residual risk, stated rather than papered over.** These rules stop
*executable* persistence, not *interpreted* persistence: a signed `bash`
reading an attacker-modified `.bashrc`, or a Python script in `/home`. No OS
fully solves this. Bounds: declared autostart means something must actively
invoke it; the writable layer is snapshot-able (U1); provenance identifies what
wrote it and when (U6); and worst case it acts with the personality's
capabilities, which is a scoped box, not the system.

**U20 therefore holds for executable code everywhere, including the Linux
tier.** Interpreted persistence is a documented bounded residual.

### D14: Shell architecture

| Layer | Language | Runs as |
|---|---|---|
| Compositor | Zig | **native component** |
| Rasterizer | Zig (~5-10k, analytic-AA scanline) | native |
| Widget toolkit | Zig, **reactive/declarative** | native or AOT-WASM |
| Shell logic (launcher, window policy, intent bar) | Zig | WASM component |
| Command shell | `bash` unmodified via D11 | Linux personality |
| Text shaping and rasterization | **vendored FreeType + HarfBuzz** | — |

**The compositor must be native.** U9 promises a scheduler-enforced
input-to-photon budget. WAMR in interpreter mode runs 10-50x slower than
native; compositing 1080p at 60fps through an interpreter will not make the
frame budget. This revives the native component tier that D6 dropped — a
better fit here, since the compositor is small, first-party, and auditable,
unlike a vendored web engine. AOT-compiled WASM is the fallback if committing
to an AOT toolchain proves preferable.

**Text is vendored.** HarfBuzz (shaping) and FreeType (rasterization) are each
~100k lines and are what Chrome, Firefox, Android, and GNOME all use. A
first-party Latin-only text stack would mean an OS that cannot render most of
the world's languages, which is not "user-friendly." Same bargain as WAMR.

**Toolkit is reactive/declarative** (SwiftUI/Flutter-shaped): best developer
experience, fits the component model, and the accessibility tree falls out of
the node graph naturally.

**Accessibility is a day-one requirement, not a later feature.** "User-friendly"
includes screen readers, which need an accessibility tree built into the
toolkit from the start. Retrofitting one is brutal — it is why accessibility
remains poor on so many platforms.

**Shell logic stays a WASM component** so it is restartable, sandboxed, and
hot-swappable (U3): the launcher can be replaced on a running system without a
reboot.

### D15: GPU — all three tiers, via DRM uAPI plus vendored Mesa

Full 3D acceleration is in scope: modesetting, 2D acceleration, and 3D.

**Architecture: write the kernel half, inherit the userspace half.** A 3D stack
splits into a kernel driver (modesetting, GPU memory management, command
submission, fences, scheduling) and a userspace driver (Vulkan/OpenGL plus a
shader compiler). The userspace half is the harder one — a SPIR-V-to-GPU-ISA
compiler is a serious compiler project.

Mesa is userspace and speaks to the kernel through DRM ioctls. So if the Jani
kernel driver exposes a DRM-compatible interface, then with D11 in place:

- Mesa's **ANV** (Intel Vulkan driver) runs unmodified
- **Zink** provides OpenGL on top of that Vulkan
- **Chromium's GPU process** works — genuinely accelerated browsing
- **No shader compiler is written**

This is the third time D11 has paid for itself, after Chromium (D6) and
`wpa_supplicant` (D12). The pattern is now a design principle:

> **Implement Linux's kernel interfaces; inherit Linux's userspace.**

Every kernel-side uAPI implemented pulls a large, battle-tested userspace stack
across the boundary at no cost. DRM ioctls are therefore a deliberate export —
the socket Mesa plugs into — exactly as nl80211 is for `wpa_supplicant`.

**Scoping.** Linux's `i915` is ~150k lines, mostly many GPU generations, every
display output type, power management, and a decade of hardware workarounds.
Narrowing hard: **one Intel generation** (Xe / Gen12, Tiger Lake and later),
**Vulkan path only** (OpenGL arrives via Zink), skipping multi-GPU, exotic
outputs, and aggressive power management initially. Estimated **40-70k lines**,
versus 200k+ for writing both halves.

**Costs that remain.** QEMU provides no meaningful Intel GPU emulation, so this
is the one driver developed against real hardware — D1's hardware-honest
approach does not reach it. Develop against `virtio-gpu` for general shape,
then write the real driver on metal. It is the largest single item in the
project, larger than the Linux ABI layer. And one generation means a narrow HCL
entry: machines outside it fall back to the UEFI framebuffer with no
acceleration.

## Driver scope

**The organizing fact: most PC hardware speaks standards.** One driver per
*class* covers every device in that class. Only three categories are per-device
— wifi chips, GPU vendors, and fingerprint readers — and those are where all
the real cost lives.

### Standards-based (one driver covers everything in the class)

| Driver | Covers | Est. lines |
|---|---|---|
| NVMe | every modern SSD | ~1k |
| AHCI | every SATA drive | ~1k |
| XHCI | transport for every USB device | ~3k |
| USB HID | **every wired keyboard and mouse ever made** | ~1k |
| PS/2 (i8042) | older laptop internal keyboards | done (Phase 0) |
| I2C-HID | modern laptop trackpads and touchscreens | ~1.5k |
| EDID parser | **every monitor** | ~500 |
| PCIe enumeration | device discovery | ~500 |
| ACPI subset | shutdown, reboot, IRQ routing, battery, lid, backlight, thermals | ~2k |
| IOMMU (VT-d / AMD-Vi) | D3 | ~1.5-2.5k |
| Intel HDA | most built-in audio | ~1.5k |
| USB Audio Class | every USB headset and DAC | ~1k |
| UVC | every modern webcam | ~1.5k |
| SDHCI | every SD card reader | ~1k |
| Bluetooth HCI transport | see note below | ~1k |
| **Subtotal** | | **~18-22k** |

### Semi-standard (a handful of chips covers most machines)

| Driver | Covers | Est. lines |
|---|---|---|
| Intel e1000e | most Intel built-in ethernet | ~1.5k |
| Realtek RTL8169 | most Realtek built-in ethernet | ~1k |
| CDC-ECM / CDC-NCM | standards-compliant USB ethernet | ~1k |
| ASIX AX88179 | most USB-C docks and dongles | ~1k |
| **Subtotal** | | **~4.5k** |

### Per-device (the expensive categories)

| Category | Est. lines | Notes |
|---|---|---|
| Wifi (D12) | 60-100k | 802.11 MAC + nl80211 + Intel, MediaTek, Realtek, Atheros, Broadcom |
| GPU (D15) | 120-210k | 40-70k **per vendor**: Intel, AMD, Nvidia |
| Fingerprint | ~0 | `libfprint` is userspace over USB — runs unmodified via D11 |
| Thunderbolt / USB4 | — | complex, deferred |

**Fingerprint readers cost nothing beyond USB.** `libfprint` and `fprintd` are
userspace and speak to readers over USB directly, so with D11 plus USB device
access they run unmodified — Validity, Synaptics, Goodix, and Elan covered with
no kernel code. Fifth instance of the design principle, after Chromium,
`wpa_supplicant`, Mesa, and BlueZ. Caveat: match-on-chip readers with secure
enclaves are not fully supported by `libfprint` either and remain unsupported.

**GPU vendor staging.** The D15 architecture is identical for all three —
implement each vendor's DRM uAPI and Mesa's driver for that vendor runs
unmodified, with no shader compiler written in any case. Order: **Intel**
(best public documentation, in most laptops, ANV), then **AMD** (very good
open documentation, RADV is excellent, covers APUs and discrete), then
**Nvidia** (least documented, open kernel modules only since 2022 for Turing+,
NVK still maturing). All three is ~120-210k lines — the entire rest of the
project again. Let real demand decide whether to go past Intel.

**Unsupported GPUs must degrade, not break.** Any machine falls back to the
UEFI framebuffer and gets a working unaccelerated display. "We support Intel"
must mean *slower* on AMD, never *dead* on AMD. This is a deliberate design
property, not an accident.

**Bluetooth applies the design principle.** HCI transport over USB is a
standard (~1k lines), but the stack — L2CAP, SDP, HID profile, A2DP — is
20-30k. BlueZ is userspace and speaks HCI over `AF_BLUETOOTH` sockets, so
implementing the socket family and HCI transport makes **BlueZ run
unmodified**. Fourth instance of the principle, after Chromium,
`wpa_supplicant`, and Mesa.

**Printers need zero kernel code.** IPP is network-based and CUPS runs in the
Linux personality via D11.

## Scope impact

| Addition | Est. lines (yours) | Phase |
|---|---|---|
| Drivers (incl. IOMMU) | 14-21k | 4 (written) / 10 (hardened) |
| Installer, pre-flight, HCL, recovery | 2-4k | 10 |
| WASI shim | 2-4k | 3 |
| Signing, declarative system state, resume verification (D7) | 2-3k | 4 |
| Linux ABI, complete surface (D11) | 60-120k | 9 |
| Wifi, five chip families (D12) | 60-100k | 10 |
| GPU, three vendors (D15) | 120-210k | 10 |
| ARM64 port (D16) | 50-80k | 10 |
| Shell: compositor, rasterizer, toolkit (D14) | included in Phase 8 | 8 |
| **Total added** | **~290-510k** | |

Against the blueprint's original 25-50k, the target is roughly **340-600k lines
of first-party code** — genuine Linux 2.0 territory (~750k), reached by
building more rather than by padding.

### D16: Core versus coverage tail

At this scale, how the work is *organized* matters more than the total. The
scope splits into two categories with completely different properties:

| | Lines | Character |
|---|---|---|
| **Core** | ~110-210k | Deeply interdependent, must be sequential, must be first-party |
| **Coverage tail** | ~230-390k | Additive, independent, parallelizable |

**Core** is everything through Phase 9, plus Intel GPU, ath9k_htc and Intel
wifi, the ~100-syscall interpreter core, x86_64 only, the installer, recovery,
and the shell. Its pieces constrain each other — the object store constrains
recovery, which constrains the snapshot format — which is why sequencing has
governed this design throughout.

**Coverage tail** is AMD and Nvidia GPU drivers, MediaTek/Realtek/Atheros/
Broadcom wifi, the remaining ~250 syscalls, and ARM64. None of it blocks
anything else. A Realtek wifi driver depends only on the 802.11 MAC interface;
ten people could write ten drivers simultaneously without coordinating.

**Implication:** the coverage tail is the natural home for contributors and the
part that parallelizes. This is exactly how Linux crossed the same threshold —
Linus wrote a core, the community wrote the tail, and by 2.0 the driver tree
dominated the line count and was almost entirely other people's work. Past
roughly 200k lines the lever is contributors, not hours.

**Therefore Phase 10's milestone is "boots on machines I own,"** with broader
coverage as continuous work afterward rather than a gate beforehand. Shipping
must not wait on the tail.

**Timeline honesty:** at sustained professional output on systems code
(10-50 lines/day shipped and tested; kernel work sits at the low end),
340-600k lines is a 13-23 year solo effort. The core alone is 4-8 years
full-time. Recorded so it is a decision rather than a discovery.

Calibration, since the number matters: Linux 0.01 was ~10k lines, Linux 1.0
(1994) ~176k, Linux 2.0 (1996) ~750k, and Linux today ~30M. This target sits
between Linux 1.0 and 2.0 — a from-scratch kernel of proven, historically
achieved scale, reachable solo over years. It is emphatically not "Linux size"
in the modern sense; most of Linux's 30M lines are drivers for hardware Jani
will never support, plus a dozen CPU architectures and sixty filesystems.

The single largest lever against this number is D15's architecture, and the
principle behind it generalizes: **implement Linux's kernel interfaces,
inherit Linux's userspace.** Chromium, `wpa_supplicant`, and Mesa each arrive
free that way. Before writing any large userspace subsystem, check whether an
existing kernel uAPI would pull a mature implementation across instead.

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
2. **Firmware blob distribution** for Stage 2 wifi (D12) — licensing and
   packaging, following `linux-firmware`'s model. D15 raises the same question
   for GPU firmware.
3. **Snapshot retention policy.** How many, how long, and what evicts them.
4. **Declarative system state format (U17).** Load-bearing for D7 and D13.
5. **DRM uAPI version target** (D15). The i915 and newer `xe` kernel interfaces
   differ; which one Mesa is expected to bind against determines the driver's
   shape and needs deciding before Phase 10 work starts.
6. **C++ as a first-party app and visual-component language.** Raised
   2026-08-05, **deferred to Phase 8**, when the desktop environment is built
   and the requirements of real rendering work are known. Deciding earlier
   would be deciding without that data.

   C++ is not excluded by the determinism contract. Tier 2 excludes
   managed-runtime languages for GC and scheduler conflicts; Tier 3 excludes
   JIT and OS threads. C++ needs none of these, so it is technically Tier 1
   capable. The blueprint's preference for Zig is stated as SDK economics —
   one language spanning kernel, services, and apps — not as a capability
   limit.

   **The gate is hot-swap (U3), and it should be settled before anything
   else.** Hot-swap replaces a component's code while its persisted linear
   memory survives. C++ objects in that memory hold vtable pointers into the
   old module; after a swap they are stale, and silently so, because the
   memory remains structurally valid. Zig's explicit dispatch does not create
   this. If C++ cannot answer this, the choice is not a language preference
   being declined — it is a pillar being traded away.

   Two further costs, both specific to this system rather than to C++ in
   general. Linear memory size is the per-tick disk write cost, so C++ runtime
   setup, static initialization, and heap are paid on every commit — the same
   trap as Zig's default 1 MiB stack, which made a fifty-byte component cost
   1.08 MB per tick. And the payload law (object IDs and offsets, never
   language pointers) runs against C++ idiom: `std::string`, `std::vector`,
   and `shared_ptr` are pointer-dense, so a C++ SDK would fight the language's
   defaults to make obeying the law easier than breaking it, which is the
   SDK's stated job.

   Nothing needs to be built now to keep this open. D3.15 in the 2026-08-05
   IDL spec separates `idlc`'s emitters behind one interface, so an `sdk/cpp`
   would be an addition rather than a rewrite.

**Resolved during design:**

- *Does D11 weaken D7?* — resolved by **D13**. The personality's root image is
  signed and immutable, W^X applies at the personality level, JIT is a
  capability, and autostart is declared. U20 holds for executable code
  everywhere; interpreted persistence remains a bounded, documented residual.
- *GPU for Chromium* — resolved by **D15**. All three tiers are in scope, and
  Mesa runs unmodified over a DRM-compatible uAPI, so Chromium's GPU process
  works rather than falling back to software rendering.
- *C++ runtime for a native component tier* — moot **as originally posed**. No
  browser is ported (D6), and the tier now exists for the first-party Zig
  compositor (D14). Note that this resolution answered a narrow question:
  whether C++ was needed to *host a ported browser*. It did not consider C++ as
  a first-party app language, which is open question 6 above.

## Next step

Snapshot on-disk format under D8's constraint is the immediate blocker: it
gates recovery design and it is Phase 2 work in progress right now. The
implementation plan should start there.
