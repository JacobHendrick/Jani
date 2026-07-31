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

## 2026-07-12 — Phase 1 memory foundation and bump heap

Committed PMM/VMM work is green: the PMM passes 32 hosted checks under
ASan/UBSan, the VMM maps/translates/unmaps 4 KiB pages, and page faults name
the permanent address-space region they hit. Added the first kernel bump
heap at `MEMORY_LAYOUT_HEAP_BASE`, with a kernel PMM/VMM page backend and a
hosted arena backend. Hosted tests cover invalid setup, alignment, writable
allocations, cross-page growth, and exhaustion. QEMU verifies a writable
allocation spanning two pages. This bump stage intentionally has no
`kfree`; the next allocator lesson adds reusable free blocks.

## 2026-07-12 - Reusable heap blocks and size classes

Extended the bump heap with hidden allocation headers, `kfree`, double-free
rejection, and reusable free blocks. The free list now groups blocks into
nine size classes, allowing small requests to avoid consuming much larger
freed blocks. Added `krealloc` with in-place shrinking, growth that preserves
existing data, and failure behavior that leaves the original allocation
untouched. Hosted PMM and heap tests pass 32 and 63 checks under ASan/UBSan,
and the freestanding kernel links with the same allocator implementation.

Added a libFuzzer harness that drives random `kmalloc`, `kfree`, and
`krealloc` sequences against 64 live allocation slots. Every live block is
filled with a known byte pattern and checked after allocator operations, while
ASan/UBSan watches for memory-safety errors. `make fuzz-heap` runs a bounded
smoke campaign; `FUZZ_RUNS` can be raised for longer campaigns.

The Phase 1 milestone is green in QEMU. The bare-metal self-test confirms
cross-page allocation, exact-address free-list reuse, realloc growth with all
5,000 original bytes preserved, and shrink-in-place behavior. Boot continues
through PMM verification, interrupt setup, and timer ticks. Phase 1 is complete.

## 2026-07-15 — Phase 2 begins: WAL protocol modeled and checked

Per blueprint rule R9, modeled the WAL commit/recovery protocol before
writing store code. `WalCommitAbstract` states the atomic-durable promise;
`WalCommit` models tagged disk sectors, a volatile write cache, arbitrary
subset crashes, torn records, ordered recovery, and checkpoint truncation.
TLC checks type safety, visible-version integrity, durable recoverability,
history agreement, and refinement to the abstract machine. The positive
model is green over 138,767 distinct crash/recovery states.

Crash exploration exposed a useful format rule before implementation: every
sector value needs an explicit tag. Mixing raw strings, integers, and records
made torn states type-unsafe, so the model now uses tagged values such as
`<<"free">>`, `<<"sb", n>>`, and `<<"home", id>>`. Recovery must replay WAL
slots in order, and checkpoint must never advance past an in-flight record.

`make model-check-negative` proves the model has teeth. TLC catches all three
seeded bugs: acknowledgement without a WAL flush, superblock truncation before
home entries are durable, and recovery that trusts a record without checking
all parts and its payload. The no-flush trace fails exactly after an ack makes
a commit visible while the durable platter still contains no recoverable copy.

## 2026-07-16 — Phase 2 object table foundation

Added the fixed-capacity in-memory object table: IDs are compared as 128-bit
values, entries stay sorted by ID, and binary search finds an entry without
scanning the whole table. An upsert inserts a new ID or replaces an existing
entry only when its version number is newer. Hosted tests cover sorted inserts,
lookups, replacement, invalid entries, and full-table rejection (28 checks).
The same C modules compile and link into the freestanding kernel.

## 2026-07-23 — Snapshots, reclamation, and a crash fuzzer

Snapshot create, rollback, get, and discard landed, which completes the
store API. A snapshot copies nothing: it is an `(id, table_sector,
table_count)` triple in the superblock, so boot-time C can roll back without
a mounted table (D8). Rollback keeps newer snapshots rather than truncating
them — branching timelines, and reversible if that turns out wrong.

Replaced the bump allocator with a free-space bitmap, because the old one
only ever grew and the disk filled permanently. The bitmap is **derived, not
persisted**: it is rebuilt at mount from the live set reachable through the
superblock. A persisted bitmap would need its own WAL protocol, since a torn
bitmap write could mark live data free and the next allocation would
overwrite a committed object. A derived bitmap has nothing on disk to tear.
This deliberately deviates from the blueprint's on-disk allocation bitmap.
Garbage collection falls out for free — `object_store_collect` is just the
rebuild, so it is crash-safe with no sweep list and no refcounts.

Added a crash-injection fuzzer. The structure is the check: every iteration
remounts from disk before acting, so the remount *is* the recovery test, and
a dangling WAL record left by a cut-off commit gets replayed by the next
iteration's mount. Writes during mount get an unlimited budget (a clean
reboot with a working disk); the budget is injected only during the operation
under test. Honest scope: it asserts mountable-and-consistent after every
crash, not that the recovered state is exactly the pre- or post-op state —
that needs a shadow model. 200k runs clean under libFuzzer + ASan/UBSan.

Hand-written free-space helpers needed a review pass before they worked: an
`8u` typo, a missing semicolon, a dropped `write_sector`/`flush` NULL check,
a memset typo, a stray brace, and a missing `bitmap_clear_range`.

## 2026-07-29 — Arena read cache, and a repo that did not build

The read cache was one object wide — `cache_valid` plus three fields,
invalidated on every mutation. Replaced it with a caller-supplied byte arena
and a 16-slot directory keyed by `(id, version)`, evicted least-recently-
used. Keying on version buys invalidation for free: after a rollback a stale
slot simply fails the version check and reloads, so the explicit
`cache_valid = 0` calls are gone from put's and rollback's error paths.

Arena extents are padded to `_Alignof(struct object_header)`. `get` casts
`arena + offset` to a header pointer, and `put` inserts objects of length
`128 + payload`, which is not necessarily aligned — without padding the
*second* slot onward would be misaligned and UBSan would trap. The first
insert would look fine, which is the annoying kind of bug.

Four cache-pressure tests, each verified by mutating the implementation and
confirming the test caught it (149 -> 476 checks). The mutation that matters:
**removing the oversized-object guard does not admit a large object, it
hangs.** `cache_insert` evicts until the arena is empty, `cache_evict_lru`
then returns early at zero slots, and `(0 + span) > capacity` stays true
forever. In the kernel that is a lockup with interrupts still enabled, not a
crash you can read a backtrace from. The guard has to stay above both
eviction loops; a future tidy-up that folds it into the loop reintroduces it.

Discovered while looking for where things stood: **the repo did not build
from a clean clone and had not for two weeks.** The Makefile referenced
`kernel/mm/heap.c`, the whole object foundation, four test harnesses, the
heap fuzzer, and three `WalCommitBug*.cfg` files that were never `git add`ed
— not gitignored, just never staged. Roughly two sessions of Phase 1 and
Phase 2 work existed only in the working tree, one `git clean` from gone.
Committed in three pieces before touching anything else. Lesson: `git status`
at the *end* of a session, not the start of the next one.

Retracted, same day: I claimed `put` leaks stale staging-buffer bytes into
the tail of an object's last sector, because it memsets only
`128 + payload_size` while writing whole sectors. That is wrong.
`write_object_bytes` zero-fills its per-sector stack buffer before each
`memcpy`, so the padding written to disk is always zero and the staging
buffer's contents never reach it. Added a test that reads the raw disk bytes
behind a short object and asserts the tail is zero and specifically does not
contain the previous object's fill byte; deleting the zero-fill makes it
fail. Second wrong reading of this file in one session — both times from
reasoning about a call site without opening the callee.

Next: virtio-blk, the last Phase 2 milestone item and the store's first real
disk. Transport decision: **modern virtio (1.0+) over MMIO, polled** — no
legacy port-I/O path, no MSI-X until the Phase 4 driver-component model has
somewhere to route interrupts.

## 2026-07-29 (later) — virtio-blk, and the SSE trap that was always waiting

Phase 2 is functionally complete: the object store runs on a real disk.
Modern virtio over MMIO, polled — no legacy port-I/O path, and no MSI-X until
Phase 4 has somewhere to route interrupts. PCI enumeration, capability walk,
feature negotiation, split virtqueue, three-descriptor block requests.

Verified in QEMU on an 8 MiB virtio-blk disk: format, put, snapshot, mutate,
rollback. Then a second boot on the same image mounts the store and finds
version 1 with its 3-byte payload intact. That second boot is the real
result — it is the first evidence that a flush in this system actually
reaches the platter, which every crash-safety claim depends on.

FLUSH is a hard requirement at init, not a nicety. If the device does not
offer `VIRTIO_BLK_F_FLUSH` the driver refuses to come up, because the WAL
commit protocol's entire durability argument assumes flush is a barrier, and
a store running without one is fiction that no hosted test can detect (both
fake disks treat flush as a no-op returning success).

**The bug that had been waiting since Phase 0:** the kernel image contained
674 SSE instructions and never enabled SSE. Nothing had tripped it because
nothing copied a struct big enough for the compiler to reach for `movups`.
The first call into the store did — `object_store_io` is passed by value —
and the kernel took a #UD. So the kernel had never been *able* to call the
object store API; the hosted tests could not see this because Linux enables
SSE for us.

Fixed by keeping SSE out of kernel code rather than enabling it in CR0/CR4,
which means interrupt handlers never have to save XMM state. Toolchain trap
worth remembering: **`-mgeneral-regs-only` is accepted and silently ignored
on x86** by this clang. It compiles, it looks right, it does nothing. The
flags that work are `-mno-sse -mno-sse2 -mno-mmx`, and `-mcpu=x86_64-sse-
sse2-mmx` for the Zig objects. Verified by counting `xmm` in the disassembly,
which is the only way to know.

Every request bounces through a single DMA page instead of mapping caller
buffers directly, because `object_store_io` hands the driver arbitrary kernel
pointers that can straddle a page boundary. One memcpy per sector, one whole
class of bug gone.

Still needs Jacob: real hardware, and crash-consistency under a kill
mid-write. QEMU agreeing is not hardware agreeing.

## 2026-07-31 — Phase 2 milestone gate: crash test in QEMU

`make crash-test` closes the blueprint's Phase 2 milestone. It boots the
kernel against a persistent virtio-blk image, kills QEMU at a random moment
during a write workload, and makes the *next* boot the check: mount, then
verify every table entry is readable, its header version matches the table,
and its payload matches the pattern that version implies. 20 cycles pass, ~190
interrupted workload rounds.

The kernel demo grew a self-verifying workload (4 objects, payload derived
from version) and periodic `object_store_collect`, so GC runs under crash too.

Two things worth writing down.

**The verifier has teeth, checked rather than assumed.** Corrupting sectors
3-10 changed nothing — those had become stale COW versions, correctly ignored.
Only when I found every object header on disk by its magic and flipped a
payload byte in each did it fail, with `entry 0 is not readable`. Good: it
means the check tracks live data, not whatever happens to be lying around.

**Killing QEMU is not a power cut, and I can prove it.** With
`CACHE_MODE=unsafe` — QEMU discarding every flush request outright — the crash
test still passes every cycle, because the host page cache keeps writes
regardless of what the guest asked for. So this gate covers crash-consistency
of the commit *sequence*, unmountable stores, torn objects, and cross-object
clobbering. It does **not** cover whether flush is a real durability barrier.
A driver whose flush did nothing would pass. That assumption is now the single
largest unverified thing in the store, it needs real power loss on real
hardware, and everything `WalCommit.tla` proves rests on it.

Also: the first version of the harness hung forever, because the kernel never
halts — it falls into the timer tick loop — and the final verification boot
had no kill. Every boot is now bounded, either by the chosen kill moment or by
polling the serial log for a verdict.

## 2026-07-31 (later) — WAMR is in the kernel, and -O0 has opinions about math

Tasks 6 and 7 of the beachhead plan. WAMR 2.4.5 is vendored, ported onto
kernel services, and linked into `jani.elf`, which still boots and still
passes 25 crash cycles. Nothing executes WASM yet — that is Task 8.

The plan left these two tasks deliberately vague, on the grounds that writing
a file list from memory for a tree nobody had read would be worse than
admitting the gap. That was the right call: reading the tree contradicted the
plan three times.

**The trampoline the plan forgot.** `arch/invokeNative_em64.s` is not in the
plan's file list, and on x86-64 it is the hand-written assembly that marshals
wasm operands into SysV registers for a host call. Task 8's entire purpose is
calling a host function; without this file it could not have worked. The C
fallback exists but upstream documents it as unreliable on x86-64 precisely
because some arguments go in registers rather than on the stack.

**`aot_runtime.h` is included unguarded** by six files even with AOT off, and
it pulls `compilation/aot.h` behind it. Both are vendored as headers. Neither
drags in LLVM — they are type definitions. And `wasm_c_api.c` could not be
excluded after all: the classic interpreter itself calls
`wasm_runtime_invoke_c_api_native`, which needs `wasm_trap_delete`. Writing
our own would have been a stub pretending to be upstream's semantics.

**The math shim did not survive the kernel's `-O0`.** Task 2 asserted that
`__builtin_sqrt` and friends "compile to single instructions now that SSE is
on." True at `-O2`; false at `-O0`, where clang emits calls to libm. The
freestanding link failed on `sqrt`, `ceil`, `floor`, `trunc`, `rint` and
their float variants. The hosted tests could not see this — the host links
libm, so the shim's tests passed while the kernel could not link.

The obvious fix was the trap. SSE4.1's `roundsd` does floor/ceil/trunc in one
instruction, and `QEMU_FLAGS` pins `-cpu qemu64`, which does not have SSE4.1.
That would have converted a link error into a `#UD` at runtime in a driver
somewhere — the same shape as the SSE bug from two sessions ago. So `sqrt`
uses `sqrtsd` (SSE2, which `start.S` already guarantees) and the rounding
functions are portable C.

**`qsort` is where the untrusted bytes are.** WAMR's module loader sorts
export *names*, taken straight from the module, to detect duplicates. A
fixed-pivot quicksort there is an algorithmic-complexity attack a module can
trigger on purpose, during load, in kernel context. It is introsort now —
median-of-three quicksort falling back to heapsort past a `2*log2(n)` depth
limit — recursing into the smaller partition and looping on the larger, so
stack depth is `O(log n)` on a 256 KiB boot stack rather than `O(n)`. The
tests use the shapes that motivated it: 4096 elements all equal, fully
reversed, organ-pipe.

**`make verify-wamr`** re-downloads the pinned tarball, checks its sha256, and
proves all 75 vendored files match upstream byte for byte. The vendored tree
is the curated subset — 26 compiled units — so the pin means something it
otherwise would not once files are pruned.

Two known gaps, both Task 8's problem before any untrusted module runs:

- `os_thread_get_stack_boundary` returns NULL. Upstream allows it and
  branches on it, but WAMR then cannot detect native stack overflow. Deep
  recursion is bounded by WAMR's wasm stack limit only, not by the real
  stack.
- `os_time_get_boot_us` is PIT ticks at 100 Hz, so 10 ms resolution. Fine for
  logging, useless for the U9 latency budget.

Also worth a note: `KERNEL_OBJECTS` uses `:=`, so referencing the WAMR object
lists inline in that assignment expanded to nothing and cheerfully linked a
kernel with no WAMR in it. The tell was collapsed whitespace in the `ld` line.
They are appended with `+=` after the definitions now. Make's two assignment
operators are exactly the kind of silent-success failure this project keeps
running into.

Gates: `make test` 3,258 checks (was 1,108), `make kernel` links,
`make crash-test` 25/25, both fuzz campaigns clean, `verify-wamr` clean.

<!-- Next entry goes here -->
