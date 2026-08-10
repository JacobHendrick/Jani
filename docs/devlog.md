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

## 2026-08-02 — A WASM module runs in the kernel, and mmap was never there

Task 8. `hello from a WebAssembly module` comes out of the serial port,
produced by the module, through a host function, inside the kernel. Phase 3
slice 1 of 4 is done.

Five things were wrong. One was in the code written for the task. The other
four were invisible by construction — none of them a logic error, all of them
something that compiled, linked, or booted while doing nothing.

**The bounds check that checked and then proceeded.** `jani_log` validated the
sandbox-supplied `(offset, length)` against linear memory, set an exception on
failure, and then read the pointer anyway — the `return` was missing.
`wasm_runtime_set_exception` does not unwind; it sets a flag the interpreter
notices when control comes back. So the violation was recorded and permitted
in the same breath. The one bug in this slice that a compiler could never have
found, in the one function whose entire purpose is the trust boundary.

**Linear memory does not come from the allocator you give WAMR.** This is the
big one. `wasm_runtime_full_init` takes malloc/realloc/free, and they serve
WAMR's internal structures only. Linear memory goes to `os_mmap` directly
unless `WASM_MEM_ALLOC_WITH_USAGE` is set, and `os_mmap` was a stub returning
NULL from the platform port. No module with a memory section could ever have
instantiated. The port was declared done two sessions ago and linked fine,
because nothing had asked it for memory yet.

**WAMR never zeroes linear memory.** There is no `memset` anywhere in
`memories_instantiate`. It relies on `mmap` returning zero-filled pages — true
on every hosted OS, false for `kmalloc`. Backing linear memory with the kernel
heap without zeroing would have handed every module a 64 KiB window onto
recycled kernel memory: freed object-store payloads, WAL buffers, whatever
`kfree` last released, readable from inside the sandbox, in the slice whose
whole job is to establish that sandbox. Under `mmap` this bug cannot exist,
which is exactly why it is not guarded against upstream.

That decided the fix. `WASM_MEM_ALLOC_WITH_USAGE` looked like the official
route, but `realloc_func` receives only the new size — it cannot know where the
old data ended, so it cannot zero a grown region on `memory.grow`.
`os_mremap(old_addr, old_size, new_size)` receives both. The `os_*` surface is
the correct fix, not the workaround. `os_mprotect` returns 0 only for the
read|write request WAMR actually issues, `-1` otherwise, so a future
`PROT_NONE` fails loudly instead of silently not protecting.

**A UBSan alignment trap inside vendored WAMR.** `ud1` at
`tables_instantiate`, which reads as "invalid opcode" but is the sanitizer
trap, not the CPU rejecting an instruction. Confirmed under gdb rather than
guessed: `first_table = 0xffffd000000001ec`, low three bits 4. WAMR places it
at `global_data + global_data_size`, rarely 8-aligned, then stores 8-aligned
fields through it. Benign on x86-64, invisible everywhere else because nobody
compiles WAMR under `-fsanitize=undefined`. Vendored objects now build with
`-fno-sanitize=alignment` — that one check, those files only. `runtime.c`
builds with plain `CFLAGS` and keeps everything. Worth revisiting if the kernel
ever leaves `-O0`: with SSE on, an auto-vectorized store through a misaligned
pointer stops being a sanitizer complaint and becomes a real `#GP`, which is
the same trap this project already hit once from `start.S`.

**The heap check could not have passed.** The plan's definition of done said
heap usage must return to its pre-run value, and `kheap_used_bytes` returns
`heap_next - heap_base` — a bump-pointer high-water mark that never decreases.
Frees go to a free list; the mark only advances. The criterion was unmeetable
by any correct implementation, and the 1.2 MB it reported was not a leak. The
line reports now rather than asserts. A real test still matters, because the
plan itself notes slice 2 instantiates components repeatedly: instantiate,
tear down, instantiate again, and assert the high-water did not move the second
time. Not yet written.

Three wiring gaps, all of which fail later than compile, found by trial-linking
the kernel against a stub `runtime.c` before the real one existed:

- `limine.conf` had no `module_path`, so the ISO shipped `hello.wasm` and
  nothing loaded it. The commit that put it in the ISO tree was green.
- `MODULE_VALIDATE_OBJ` was defined but never in `KERNEL_OBJECTS`.
- `runtime.c` had no object rule.

And a symbol collision that had been latent since the validator landed:
`wasm_module_validate` is already public in the WebAssembly C API
(`wasm_c_api.c:2370`). Two definitions coexisted quietly because
`module_validate.o` was never linked. Adding the missing object is what made
them meet. Namespaced to `jani_wasm_module_validate`.

Also restored an `object_store_get` in `run_store_demo` that an edit had
overwritten. It never failed — it left `header` and `payload` holding the
previous get's values, so the version-2 assertion was checking stale data. A
silent weakening of a check inside the demo `make crash-test` runs 25 times.

`os_thread_get_stack_boundary` now returns a real value: the machinery was
already committed and nothing had ever called `kernel_stack_set_size`, so it
returned null forever. `os_time_get_boot_us` is still PIT ticks at 100 Hz.

Gates: `make test` 3,258 checks, `make kernel` links, `make crash-test` 25/25
with `RECOVERY OK`, both wasm fuzz campaigns clean, `verify-wamr` 75/75 byte
for byte. All three commits verified green individually, not just the tip.

## 2026-08-03 — Flush becomes testable, and the wow demo gets a spec

Slice 2 was designed and Jacob's half of it built. Jacob's half — the on-disk
layouts, the syscall ABI as implemented, the resume path, the tick loop — is
next and is untouched here.

**The gap that mattered most is now half closed.** "Is flush a real durability
barrier" was unverified, and unverifiable by any gate the project owned:
`make crash-test` passes with `CACHE_MODE=unsafe`, where QEMU discards every
flush, so it cannot distinguish a barrier from a no-op. The question splits in
two, and only one half needs hardware. Does the device honor the barrier is
hardware and stays assumed. Does our code issue the barrier in the right places
is software, and is now tested hosted.

`tools/hosted/test_write_ordering.c` models a disk with two images — what a
reader sees, and what survives power loss — where `flush` is the only operation
that promotes bytes between them. That makes flush load-bearing by
construction. Power-cut points are swept through a transaction and recovery
must yield the old version or the new one, never a torn one.

**Getting the negative control to fire took three attempts, and each failure
was informative rather than a bug in the test.**

- Discarding every flush trivially fails: nothing is ever durable, not even the
  format, so mount dies immediately. That is a faithful model of
  `CACHE_MODE=unsafe` and worth keeping, but it only proves the rig catches a
  totally broken barrier.
- Dropping exactly one flush found nothing. A dropped barrier only *delays*
  durability, and the next flush repairs it atomically together with whatever
  that flush covers — so the commit record can never become durable ahead of
  its data. Delay is safe; the hazard is reordering.
- Making the power cut adversarial — promote the unflushed writes *except* one,
  rather than discarding all of them — still found nothing, because the model
  only cut power *at* a write. The window between a write succeeding and its
  barrier completing was never sampled.

The fix was one line of semantics: let the Nth write succeed, then cut. With
that, dropping a single barrier corrupts **2 of 3,840** (flush, cut, drop)
triples. The narrowness is the finding. That bug class survives ordinary crash
testing indefinitely.

The store passed every version, including 240 cut × drop combinations under the
adversarial model. That is evidence about the *implementation* of the commit
protocol, which is exactly what `WalCommit.tla` cannot supply — the same gap
that hid the committed-transaction-loss bug on 2026-07-22.

Positive sweep runs in `make test` at 0.7 s. The negative controls live in
`make write-ordering-negative`, mirroring how `model-check-negative` sits
beside `model-check`, because a proof-of-teeth is not a per-commit gate.

**The object graph changed shape during design, for a correctness reason.** A
tick advances two things together: linear memory (`N` → `N+1`) and scheduling
state (clock `T` → `T+1`). As separate objects that is two transactions with a
power-cut window between them, and neither ordering survives it. Memory first
and the tick re-fires on resume — the counter advances by two. Clock first and
the tick is lost — the counter skips one. Exactness needs atomicity.

The two ways to get it are a multi-object transaction in the store, or one
object. Merging costs nothing, because memory and mailbox are *both* dirty on
every tick already, so one object writes exactly as many bytes as two. The
split is by write frequency throughout: module and capability table are
write-once and stay separate; memory, mailbox, clock and deadline are per-tick
and became one instance-state object.

**The counter component would have silently reset to zero.** If its variable
landed in a WASM global rather than in linear memory, snapshotting linear
memory would not capture it, and the demo would look correct in source while
resetting every boot. Zig normally places module-level `var` in the data
segment, but `-O ReleaseSmall` may promote a variable that is never
address-taken. Access goes through a `volatile` pointer to pin it. Verified by
parsing the emitted module: the only WASM global is a mutable `i32` (the stack
pointer), and the counter is read and written with `i64.load`/`i64.store`. A
`u64` cannot hide in an `i32` global.

`kernel/wasm/syscall_args.zig` is the second trust-boundary validator, syscall
arguments being the other untrusted input of Phase 3. Its fuzzer checks against
an independent 64-bit oracle rather than for absence of crashes, so a validator
that is merely self-consistent fails. `SYSCALL_ARGS_OBJ` went into
`KERNEL_OBJECTS` at the same time as its build rule — slice 1 lost time to
exactly that wiring gap, where `MODULE_VALIDATE_OBJ` existed but was never
linked.

Gates: `make test` 3,550 checks, `make kernel` links with all five validator
symbols present in the ELF, `make crash-test` 25/25 with `RECOVERY OK`,
`make write-ordering-negative` detects both a removed and a dropped barrier,
`fuzz-syscall-args` / `fuzz-wasm-module` / `fuzz-wasm-shim` clean at 10,000
runs each.

Still assumed, and now the sharpest single unknown in the project: that the
physical device honors a flush. Only real hardware settles it.

## 2026-08-04 — The wow demo lands, and instantiating twice finds a use-after-free

Slice 2's milestone works. A counter component ticks over serial, QEMU is
powered off outright, and the next boot continues the count. Verified across
three power cycles by hand and then turned into `make wow-demo`, which does the
same thing unattended and asserts the result.

**The gate accepts two answers, and that is the design.** The component prints
inside its handler and commits after the handler returns. A kill in that window
loses the printed tick, so the next boot repeats it; a kill after the commit
advances. Both are correct. Lower means a committed tick was lost, higher means
one was double-counted. A gate asserting a single value would fail at random
forever and eventually be switched off. Runs so far have exercised both
branches: one run repeated on two of three cycles, a later run advanced on all
three.

The gate also fails if a resume boot reports installation, or if `jani_init`
runs on resume. That is the specific way this demo could become theatre while
still printing plausible numbers.

**Four things running it found that reading it did not.**

Linear memory was 1.08 MB, not 64 KiB. Zig reserves a 1 MiB wasm stack by
default — 17 pages — and WAMR appends its 16 KiB app heap. The counter uses
about fifty bytes of it. `--stack 16384` brings the module to one page and the
instance-state object to 81,984 bytes. Worth knowing before concluding that
copying whole linear memory per tick is too expensive: here the cost was never
the memory, it was an unused stack reservation.

`jani_log` was registered as `"(ii)"` returning void while the spec declares it
returning bytes written. WAMR refused to link the import against a module
expecting `i32`. The host side was wrong. This is the first evidence for D3.6 —
declaring the whole ABI up front caught a mismatch that would otherwise have
surfaced a slice later, against components already written.

`DEMO_CACHE_BYTES` at 8192 capped objects at 8,064 bytes. `object_store_get` is
bounded by the same capacity at line 977, not just `put` at 835, so raising it
unblocks reading as well as writing.

`demo_verify_all` applied the store demo's payload pattern to every table entry,
so component objects failed it on the second boot. The pattern check is now
scoped to the demo's own four ids; the structural checks still cover all of them.

**The leak test found a use-after-free in ten seconds by doing the one thing
nothing else did: instantiating twice.** `wasm_runtime_load` **mutates the
buffer it is given**. The second load of the same bytes fails with "invalid
import kind" because the first load rewrote them. Slice 1 never noticed — it
loaded the Limine buffer exactly once per boot.

The consequence was worse than the symptom. `component_resume` copied the module
bytes into a `kmalloc` buffer, instantiated, then freed the copy — but WAMR's
classic interpreter holds pointers into that buffer for the code section. The
demo worked only because the freed memory had not been reused yet. It would have
surfaced later as impossible-looking interpreter corruption with no connection to
the free that caused it. `jani_wasm_instance_create` now takes its own copy and
`jani_wasm_instance_destroy` frees it, so the image lives exactly as long as the
module does.

`component_release` also never destroyed the WAMR instance at all, and the
failure paths in `install` and `resume` returned without releasing. Both fixed.

**The tick loop needed garbage collection.** Every tick writes a new 82 KB
version by COW and nothing reclaimed the old ones, so the store filled after
roughly a dozen ticks and the first commit after that failed. Collection now
runs every eight ticks, and `DEMO_SECTORS` went from 4096 to 16384 so an 8 MiB
region holds a useful number of versions. Worth remembering: unbounded COW
growth is the default for anything committing on a loop.

All twelve syscalls are implemented in `kernel/wasm/syscalls.c`, and the counter
exercises sixteen assertions across ten of them on first boot — create, size,
write, read, round trip, short-read clamping, out-of-bounds rejection, cap drop,
use-after-drop rejection, self, empty receive, send, receive, payload, and
attached capability. The capability table now persists, so handles survive a
resume; it is written only when dirty, keeping it off the per-tick path.

Two rough edges cleaned up. The tick loop ran inline capped at eight iterations
because `pit_init` happens later in `kmain` than the demo did; it now lives in
the idle path and wakes on `hlt`. And `component_commit` allocated and freed an
82 KB buffer every tick, which at 1 Hz forever is pointless churn; one scratch
buffer is reserved and grown only when needed.

Gates: `make test` 4,439 checks across twelve binaries, `make kernel` links,
`make crash-test` 25/25 with `RECOVERY OK`, `make wow-demo` 3/3 cycles,
`write-ordering-negative` detects both a removed and a dropped barrier, three
fuzz campaigns clean at 10,000 runs.

`make wow-demo-negative` closes the gap that entry originally ended on. Three
seeded resume bugs, each behind `-DJANI_WOW_BUG=n`: calling `jani_init` on
resume, skipping the commit, and zeroing linear memory after restore. The gate
must reject all three, and does. A gate nobody has watched fail is not yet a
gate — the same reasoning as `model-check-negative`, and the reason
`CACHE_MODE=unsafe` passing was worth taking seriously in the first place.

Still assumed: that the physical device honors a flush. Only real hardware
settles it.

## 2026-08-06 — The IDL becomes the source, and a checked gate is not the same as an adopted one

Slice 3. `idl/syscalls.idl` and `idl/records.idl` are now the single source for
the twelve syscall signatures and the four on-disk record layouts. A C tool,
`tools/idlc`, parses them and drives four independent emitters: a Zig guest SDK,
a C guest SDK, the WAMR `NativeSymbol` table the kernel includes, and a header of
`_Static_assert`s that pins the hand-written on-disk structs. Generated files are
committed; `make idl-check` fails if they drift from their source.

**The `_impl` bodies are never generated, and neither are the on-disk structs.**
The IDL owns the declarations on both sides of the boundary — what the guest
imports and what the host registers. It does not own the implementations or the
C structs, which stay hand-written and are *asserted against* the IDL rather than
produced from it. That split is deliberate: generating a struct means the
generator decides the disk format, and the disk format is permanent.

**D3.11: four arguments were declared signed and then rejected at runtime for
being negative.** `object_create`'s `size`, `object_read` and `object_write`'s
`offset`, and `timer_set`'s `delay`. Every one of those guards existed only to
undo a wrong declaration. Making the arguments unsigned deletes the guard and the
bug class together — "negative" stops being representable rather than being
caught.

**Three of the four were safe to change directly. `timer_set` was not.** The
other three have an upper bound sitting behind the sign check — `size` against
`SYSCALL_TRANSFER_MAX`, both offsets against the object's own extent — so
removing the sign check leaves something still bounding the value. `timer_set`
had nothing behind it. `logical_time + delay_ticks` in `u64` wraps, and a wrapped
deadline is in the past, so a component asking for a distant timer would get an
immediate one. The checked-deadline validator landed as its own commit *before*
the signature change, so the guard and the ABI move are separately bisectable.

**The overflow probe cannot live in `jani_init`.** `logical_time` only advances
in `component_invoke_timer`, so it is 0 when `jani_init` runs, and at 0 no `u64`
delay can overflow the sum — the reject branch is unreachable and an assertion
there passes with or without the guard. The probe runs on the first tick instead,
and derives its boundary from the clock: `headroom = ~now` makes `now + headroom`
land exactly on `UINT64_MAX` (accepted) and `headroom + 1` the first value that
overflows (rejected), so accept and reject are adjacent integers regardless of
what the clock actually reads. Verified it discriminates by replacing the guard
with a bare addition — `timer_set rejects overflow` is the only assertion that
moves.

**Two error codes moved, and nothing in the tree had ever asserted either one.**
An out-of-range offset to `object_read` or `object_write` used to hit the
`offset < 0` guard and return `EINVAL`; it now reaches the range check and
returns `ERANGE`. That is the more honest code once the argument is unsigned, but
it was a side effect of a change whose whole claim was that the ABI did not move.
A grep for `JANI_ERANGE` found it defined in `component.h` and referenced
nowhere else — the counter's bounds check asserted `< 0`, which passes under
either value. The guest now pins both codes exactly. Error codes are ABI; if
nothing asserts them they drift silently, and the drift is invisible precisely
because every wrong answer is still negative.

**The lowering rules reproduce all twelve signature strings exactly.** `(ii)i`,
`(IIi)i`, `(iiii)i`, `(i)I`, `(I)i`, `()I`, `(i)` and the rest — the generated
table came out byte-identical to the hand-written one, which is the evidence the
emitters are right. `out` became a general direction modifier rather than a
slice-specific one, and `ptr<i32>` was dropped: `out slice<u8>` and `ptr<i32>`
were two spellings for "the host writes here", and in a permanent format that
redundancy outlives its excuse. Lowering is unchanged either way.

**The lesson of the slice: generating a file and checking it is not the same as
adopting it, and only adoption is load-bearing.** For most of the slice
`make idl-check` was green and proving nothing about syscalls. It regenerates the
four outputs and diffs them against the committed copies — it never read
`syscalls.c` or `counter.zig`. A hand-edit to `jani_symbols[]` would have sailed
straight through. Records were real the whole time for an unrelated reason:
`records_conform.h` is `#include`d by `component.c`, so its assertions fire at
build time. Syscalls only became real when the second copy was *deleted* —
`syscalls.c` now includes the generated table and `counter.zig` declares no
externs at all. The equality between generated and hand-written was the proof the
generator worked; it was never a reason to keep both. A drift gate that compares
two artifacts neither of which is compiled is a gate over nothing.

Gates: `make test` 4,669 checks across thirteen binaries (`test_idlc` at 215),
`make kernel` links, `make idl-check` matches, `make idl-negative` 3/3 defects
rejected (a retyped argument, a moved record field, a deleted syscall),
`make wow-demo` 3/3 cycles with 20/20 syscall assertions and no `SYSCALL FAIL`
in the serial log (the end count varies by a tick between runs, for the reason
the 2026-08-04 entry gives), `make crash-test` 25/25 with `RECOVERY OK` and 4
objects consistent across ~181 workload rounds.

Still assumed, and unchanged by any of this: that the physical device honors a
flush.

## 2026-08-06 (later) — The determinism contract was true for the wrong reason

Slice 3 merged to `main` as a fast-forward, 16 commits. Then went looking at the
one item the slice 2 spec claimed was already done and wasn't.

`2026-08-03-wow-demo-design.md` said the determinism contract was "enforced in
the WAMR port now," and listed "NaN canonicalization enabled" first. Nothing in
the tree does it, and **vendored WAMR has no build option for it** — no
`WASM_ENABLE_NAN_*` anywhere in `core/config.h` or the sources. The classic
interpreter's float opcodes expand to plain C operators;
`DEF_OP_NUMERIC(float32, float32, F32, /)` is a literal `/=` on `float` against
the operand stack. So NaN behaviour is whatever the hardware does, and there was
no knob to turn.

**That last fact is what made this measurable on Linux instead of in QEMU.**
Because the interpreter is a straight C operator on `float`, a hosted test
compiled for x86-64 exercises the identical code path the kernel will — same
architecture, same SSE, same operators. `tools/hosted/test_determinism.c` runs
under the normal ASan/UBSan gate and reports 4,109 checks.

**The measurement splits cleanly in two, and only one half was ever a problem.**
NaN *produced* from non-NaN operands is already canonical: `0.0/0.0` and
`inf-inf` both give `0xFFC00000` for f32 and `0xFFF8000000000000` for f64 —
payload MSB set, all other payload bits zero, which is exactly WASM's canonical
NaN, whose sign bit the spec leaves unspecified. Stable across 4,096 repetitions.
NaN *propagated* through an operation keeps its payload: `0x7FC01234 + 1.0`
returns `0x7FC01234`, a signalling NaN is quieted by setting bit 22 and otherwise
preserved, and with two NaN operands the first wins. That second half is not
canonicalization and never was.

**The contract holds anyway, for a reason the original bullet didn't state: there
is no host-side source of NaN payloads.** None of the twelve syscalls accepts or
returns a float. A payload can only enter a computation from the component's own
constants or its own linear memory, and linear memory is snapshotted and restored
bit-exactly. Every payload a component can observe is one it produced itself from
deterministic inputs, so resume reproduces exactly. The residual exposure is
portability, not reproducibility: a component reinterpreting a self-authored NaN
payload as an integer would get a value a *different* engine might compute
differently. That matters only if Jani ever gains a second execution engine.

**Which is why the preconditions are now assertions rather than prose.** Four
`_Static_assert`s in `kernel/wasm/runtime.c`: interpreter on, AOT off, JIT off,
fast-interp off. Verified both that can fail actually do — `-DWASM_ENABLE_AOT=1`
and `-DWASM_ENABLE_FAST_INTERP=1` each fail the kernel build with a message
naming the spec. The JIT one cannot fail on its own, because WAMR forces
`WASM_ENABLE_JIT` to 0 whenever AOT is 0; it is guarded by the AOT assertion
rather than independent, which is worth knowing before trusting it alone.
`runtime.c` had to gain `#include "platform_common.h"` for any of this to
compile — `wasm_export.h` does not pull `config.h`, so three of the four macros
were simply undeclared.

**A detour worth recording: the first attempt measured this in the kernel and
tripped an unrelated gate.** Adding a float probe to `counter.zig` grew the
module by 479 bytes and the boot then failed with "instantiate leaked 68752
bytes on the second pass." It was not a leak. The module still declares one page
of linear memory — checked by parsing the memory section — so demand was
unchanged. The leak test asserts `kheap_used_bytes()` is *exactly equal* across
two instantiate/destroy rounds, and that high-water mark moves whenever the free
list cannot reuse a freed block. A 479-byte change to the module copy was enough
to shift allocation sizes and defeat reuse. The gate conflates "leaked" with
"allocator did not reuse," so it fails on benign fragmentation. Left as-is and
unfixed — worth knowing before anyone grows a component and reads that message
as a real leak.

Gates: `make test` 8,778 checks across fourteen binaries, `make kernel` links,
`make idl-check` matches, `make idl-negative` 3/3, `make wow-demo` 3/3 cycles
with 20/20 syscall assertions.

## 2026-08-07 — Installed components boot without their original module

`run_component_demo()` now checks the persistent component registry before it
looks at Limine's boot modules. A saved component resumes entirely from the
object store; the bundled Wasm file is required only when no component has been
installed yet. This is the first zero-install boot step and removes an accidental
dependency on keeping the installation artifact in every later ISO.

Gates: `make kernel` links, `git diff --check` is clean, and `make wow-demo`
passes 3/3 power cycles with the counter continuing from 1 through 104 while
`jani_init` runs only on the first boot.

## 2026-08-07 (later) — Zero-install is a tested artifact, not an assumption

Added a second boot image, `build/jani-resume.iso`, whose Limine configuration
declares no modules and whose ISO root contains no `counter.wasm`. The new
`make zero-install-test` gate installs the counter from the normal ISO, powers
QEMU off, then boots the same disk from the module-free ISO. The counter resumed
from 37 after ending at 36, proving WAMR instantiated the saved module object
directly from the store.

Phase 3 is now complete: interpreter, persistent component state, generated
IDL contracts and SDKs, capability-shaped syscalls, deterministic resume, and
zero-install all have executable gates.

Gates: `make test` passes 8,778 checks, `make idl-check` matches, `make
resume-iso` builds a module-free image, `sh -n` accepts the test script, `git
diff --check` is clean, and `make zero-install-test` passes.

## 2026-08-07 (Phase 4) — Capability rules begin as a pure module

Added the first independent capability-engine module. A capability is a fixed
24-byte object ID, rights mask, and badge. Validation rejects null pointers,
zero object IDs, empty rights, and unknown permission bits. Permission checks
require every requested bit, and derivation can only attenuate a parent that
already owns `GRANT`; it cannot invent access the parent lacks.

The module is linked into the freestanding kernel and has a hosted sanitizer
test covering validation, combined rights, denied rights, badge assignment,
parent preservation, and failed amplification attempts.

Gates: `make kernel` links and `make test` passes 8,803 checks, including 25
new capability checks under ASan/UBSan.

## 2026-08-08 — Phase 3 closes its missing uninstall path

The Phase 3 checkbox was premature: install, resume, and zero-install were
proven, but the blueprint also defines uninstall as deleting the module object.
There was no object-store delete operation and no component uninstall API.

`object_store_delete()` now removes an object through the same copy-on-write
table-generation and WAL root-swap protocol as writes. It handles an empty final
table, evicts stale cache entries, survives remount, recovers an interrupted
commit, and preserves deleted objects in older snapshots. `component_uninstall()`
removes the component from the registry before deleting its module, so a power
loss cannot leave boot pointing at a missing module. Root, state, capability
table, and data objects remain available for provenance and snapshot history.

The crash-test harness also had a stale timing assumption: all 25 cuts landed
before virtio initialized, so it was testing boot speed instead of recovery. It
now waits for the first workload marker, cuts at a randomized point after that,
and fails immediately if no write round was interrupted.

Gates: `make test` passes 8,882 checks, `make kernel` links, `make idl-check`
matches, `make idl-negative` rejects 3/3 defects, `make wow-demo` survives 3/3
power cycles, `make zero-install-test` resumes without `counter.wasm`, `make
crash-test` recovers after 25/25 workload cuts, and `make
write-ordering-negative` still exposes both seeded durability failures.

Phase 3 is now complete against every item in its blueprint section.

## 2026-08-09 (Phase 4) — Capability families support revocation

Added a bounded capability table with 16 slots. Root capabilities can be
inserted directly; delegated capabilities must pass the existing `GRANT` and
rights-attenuation rules. Each child records its parent slot, creating a small
capability derivation tree.

Revocation scans that tree and clears the selected capability plus every
descendant while preserving unrelated branches. The implementation is linked
into the freestanding kernel, and hosted sanitizer tests cover delegation,
denied amplification, denied delegation without `GRANT`, and recursive
revocation.

The component capability-table disk format is now version 2. It persists each
slot's parent, checks bounds, cycles, object identity, `GRANT`, and rights
attenuation while loading, and still reads version-1 tables by treating their
capabilities as roots. A real second QEMU boot mounted the existing store and
resumed the registered component through the new format.

Gates: `make kernel` links and `make test` passes 8,918 checks, including 47
capability checks under ASan/UBSan. A two-boot QEMU check reports `RECOVERY OK`
and `component: resumed` on the second boot.

<!-- Next entry goes here -->
