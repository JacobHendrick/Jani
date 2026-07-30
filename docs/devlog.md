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

Still open: `put` memsets only `128 + payload_size` bytes of the staging
buffer but writes whole sectors, so the tail of the last sector carries
whatever the previous object left there. Not a correctness bug — validation
ignores the trailing bytes — but it writes stale memory to disk and makes
on-disk images nondeterministic. Worth a memset of the full sector span.

Next: virtio-blk, the last Phase 2 milestone item and the store's first real
disk.

<!-- Next entry goes here -->
