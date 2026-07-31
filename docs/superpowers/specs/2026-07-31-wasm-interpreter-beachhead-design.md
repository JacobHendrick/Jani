# WASM interpreter beachhead — design

**Phase 3, slice 1 of 4.** Date: 2026-07-31.

## Goal

One `.wasm` module, supplied by the bootloader, executes inside the Jani kernel
and prints a line over serial through a host function.

That is the whole slice. Nothing persists, nothing is a component, and no
permanent format is committed. The purpose is to answer one question before
anything is built on top of the answer: **can a WebAssembly interpreter live
inside this kernel at all?**

The value is isolation. Phase 3 drops ~50k lines of third-party C into a
freestanding kernel with no libc beneath it. If that goes wrong, the failure
should have one plausible cause, not six.

## Success criteria

- `make run` shows the module's own output in the QEMU serial log.
- `make test` stays green (1,003 checks as of 2026-07-31, plus new shim and
  validator checks).
- `make kernel` stays green.
- `make crash-test` stays green — Phase 2 must not regress.

## Non-goals

Explicitly out of scope, and each has a later home:

| Deferred | Where it lands |
|---|---|
| Component model, handlers, mailbox | slice 2 |
| Persistence, linear memory in object space | slice 2 |
| The real syscall surface | slice 2, hand-declared; formalized in the IDL in slice 3 |
| NaN canonicalization / determinism contract | slice 2, with the syscall surface |
| IDL v1, `tools/idlc`, `sdk/c`, `sdk/zig` | slice 3 |
| Capabilities, WASI shim, zero-install | slice 3-4 |
| Driver-as-component policy | Phase 4 |

NaN canonicalization is called out because the blueprint warns it is "nearly
free at this stage and unpayable later." Deferring it one slice is deliberate;
deferring it past the determinism contract is not.

## Phase 3 decomposition

The blueprint's Phase 3 is six subsystems plus an IDL and two SDKs — too large
for one spec. It divides as:

1. **Interpreter beachhead** (this spec) — SSE decision, vendor WAMR, platform
   port, run a trivial module. ~500 lines plus vendored code.
2. **The wow demo** — component model, object-backed linear memory,
   resume-after-power-off, the first real syscalls. ~1,200 lines.
3. **Contracts** — IDL v1, `tools/idlc`, generated SDK stubs. ~800 lines.
4. **Zero-install** — module objects, instantiate-from-store. Mostly falls out
   of 2 and 3.

## Decisions

### D3.1 — SSE on everywhere, FPU state saved on interrupt

WASM has `f32`/`f64` instructions, so the interpreter must do floating-point
arithmetic. Verified 2026-07-31: under the kernel's current
`-mno-sse -mno-sse2 -mno-mmx`, a function returning `double` does not compile
("SSE register return with SSE disabled"), and `-msoft-float` does not help on
x86-64. Phase 3 therefore cannot start without reopening the flag decision made
on 2026-07-29.

**Decision:** enable SSE for all kernel code and save/restore FPU state with
`FXSAVE`/`FXRSTOR` on interrupt entry and exit.

The considered alternative was enabling SSE but confining it to WAMR, relying
on I5 (handlers are never preempted) and SSE-free interrupt handlers to make
FPU save/restore unnecessary. That is cheaper, but it is safe only as long as
no future interrupt handler ever emits an SSE instruction — a silent,
corrupting failure if it is ever violated. The chosen option costs ~512 bytes
of interrupt stack and a save/restore pair per interrupt, and is robust under
whatever preemption model Phase 4's scheduler adopts.

**Consequence — ordering.** The code that enables SSE must not itself use SSE,
and must run before any code that might. Today `linker.ld` declares
`ENTRY(kmain)`, so Limine jumps directly into C and there is no such window.
`kernel/boot/start.S` becomes the entry point: clear `CR0.EM`, set `CR0.MP`,
set `CR4.OSFXSR` and `CR4.OSXMMEXCPT`, then `call kmain`.

**Consequence — a new exception.** Setting `OSXMMEXCPT` routes SIMD
floating-point exceptions to vector 19 (`#XF`), which the IDT must handle.
Without it these surface as confusing faults elsewhere.

**Consequence — a stale check.** project documentation currently instructs that
`objdump -d build/jani.elf | grep -c xmm` must print 0. That becomes wrong and
must be removed in the same change, or it will read as a regression.

### D3.2 — Kernel-first bring-up, hosted testing for the untrusted and the fiddly

Blueprint R3 makes hosted-first the default. This slice deviates: WAMR is
brought up directly in the kernel rather than as a Linux binary first.

The accepted cost is that a failure has several plausible causes at once —
platform port, build integration, SSE setup, or the module itself — with printk
as the only instrument. Two mitigations:

- **Staged bring-up.** Every step announces itself over serial (see
  Verification), so the last line printed names the suspect.
- **Hosted tests for the two pieces that most need them**: the Zig module
  pre-validator (untrusted input, per R8) and the libc shim (buffer handling,
  per D3.4). Both are pure and free of kernel dependencies, so both compile and
  run on Linux under ASan/UBSan exactly as the object store does.

### D3.3 — The module arrives as a Limine module

`limine.conf` gains a `module_path` line; the kernel receives `{address, size}`
through a module request structurally identical to the memmap and HHDM requests
`main.c` already uses.

Chosen over embedding the bytes in the kernel image because changing the module
then rebuilds the ISO rather than relinking the kernel, and because it is the
natural feed for slice 4: the bootloader hands over bytes, the kernel copies
them into the store as a module object, and that copy *is* what U7 defines
installation to be. Loading from the store directly was rejected for this slice
as circular — nothing can write a module into the store yet.

### D3.4 — The libc shim is hand-written C, hosted-tested

WAMR's sources include `<string.h>`, `<stdlib.h>`, `<stdio.h>`, `<assert.h>`,
and `<math.h>`. Freestanding `zig cc` provides only `stdint`, `stddef`,
`stdbool`, `stdarg`, `float`, and `limits`. The kernel currently has four
functions: `memcpy`, `memset`, `memmove`, `strlen`. Roughly twenty-five are
needed.

Considered and rejected: vendoring picolibc, which would supply all of it
tested, at the cost of a second third-party build system and a large body of
code in the kernel that nobody on the project has read. Also considered:
implementing the formatting functions in Zig over `std.fmt`.

**Decision:** hand-written C, for total control and a minimal trusted base —
WAMR remains the only large foreign dependency in the kernel.

**Mitigation.** `snprintf`/`vsnprintf` are the fiddly part and a classic source
of buffer bugs. The shim is pure C with no kernel dependencies, so it gets a
hosted test binary under ASan/UBSan and a fuzz target, and is verified there
before it runs in the kernel. This keeps the riskiest hand-written code out of
the kernel-first debugging path.

### D3.5 — Classic interpreter, kernel heap, no threads

WAMR build configuration:

- `WASM_ENABLE_INTERP=1`, `WASM_ENABLE_FAST_INTERP=0` — the classic interpreter
  is simpler and deterministic (I6). Fast-interp and AOT are per-component
  performance choices for much later.
- `WASM_ENABLE_AOT=0`, `WASM_ENABLE_JIT=0` — a JIT cannot meet the determinism
  contract (Tier 3, excluded).
- `WASM_ENABLE_LIBC_WASI=0`, `WASM_ENABLE_MULTI_MODULE=0`,
  `WASM_ENABLE_SHARED_MEMORY=0`, `WASM_ENABLE_THREAD_MGR=0`.
- Allocation via `Alloc_With_Allocator` pointed at `kmalloc`/`krealloc`/`kfree`,
  which already exist. WAMR's heap is the kernel heap.

Thread and lock stubs are no-ops. That is honest rather than a shortcut: I5
makes components single-threaded by definition, and concurrency in Jani is many
components exchanging messages.

No CMake. A curated list of WAMR `.c` files compiles with `zig cc` into
`build/wamr/*.o` like every other object, configured by `-D` macros.

### D3.6 — Vendoring follows the tla2tools precedent

Pinned version, pinned sha256, `curl` to a `.tmp` path, `sha256sum -c`, and
only then `mv` into place. The verify-then-move ordering came out of a security
review during Phase 2 and applies unchanged: a failed check must never leave an
artifact where make would treat it as a valid target.

WAMR is committed to `third_party/wamr/` as the artifact of record. Unlike the
Zig toolchain (391 MB, gitignored), it is small enough to commit, and the
blueprint lists it under vendored third-party C.

## Architecture

```
Limine ──module──▶ kernel/boot/main.c
                        │
                        ▼
              module_validate.zig        (Zig, untrusted bytes, R8)
                        │
                        ▼
                   WAMR runtime          (third_party/wamr, vendored)
                    │        │
        platform port        libc shim
        (os_* → kernel)      (~25 functions)
                    │        │
                    ▼        ▼
          kmalloc / printk / PIT ticks
```

Four new units, each with one purpose:

**`kernel/wasm/shim/`** — the missing C library. Depends on `kmalloc` and
`printk` and nothing else. Independently testable on Linux; that is the point.

**`kernel/wasm/platform/`** — implements WAMR's `os_*` surface: allocation onto
`k*`, `os_printf`/`os_vprintf` onto `printk`, `os_time_get_boot_us` from the
existing PIT tick counter, locks and threads as no-ops. Pure adapter code with
no logic of its own.

**`kernel/wasm/module_validate.zig`** — structural validation of module bytes:
magic, version, section ID ordering and uniqueness, and that every LEB128
section length stays inside the blob. Explicitly *not* a semantic validator;
WAMR does that. This is the bounds boundary, nothing more.

**`kernel/wasm/runtime.c`** — the lifecycle: initialize WAMR, load, instantiate,
call the export, tear down. Registers the single host function.

## The one host function

`jani_log(ptr, len)` — bounds-checked against the instance's linear memory per
R4, then forwarded to `printk`.

It is **throwaway** and deliberately not part of the permanent syscall surface.
That surface is hand-declared in slice 2, once there is a working interpreter to
design against, and formalized in the IDL in slice 3 — the ordering matters,
because a syscall surface designed before anything has run is a guess.

`jani_log` is therefore allowed to be crude. What it must not do is quietly
become permanent: slice 2 replaces it rather than extending it.

## The test module

Written in Zig targeting `wasm32-freestanding` — already a pinned toolchain
target, and Zig is the official application language (Z3). It imports
`jani_log`, exports one function, and prints one line. Built by the Makefile.

## Verification

Staged serial output, one line per step, so a failure localizes itself:

```
fpu: sse enabled (cr0=..., cr4=...)
wasm: limine module, N bytes
wasm: pre-validation ok, M sections
wamr: runtime initialized
wamr: module loaded, X exports
wamr: instance created, Y memory pages
wasm: hello from a WebAssembly module      <- the module speaking
wamr: instance destroyed, heap returned
```

Hosted, under ASan/UBSan:

- `tools/hosted/test_wasm_shim.c` — the libc shim, especially `snprintf`
  truncation, exact-fit, and zero-length buffer behavior.
- `tools/hosted/fuzz_wasm_shim.c` — `make fuzz-wasm-shim`, random format strings
  and buffer sizes.
- `tools/hosted/test_wasm_module.c` — the validator against valid and malformed
  modules.
- `tools/hosted/fuzz_wasm_module.c` — `make fuzz-wasm-module`, arbitrary bytes;
  asserts rejection without traps or out-of-bounds reads.

Regression gates that must stay green: `make test`, `make kernel`,
`make crash-test`.

## Who writes what

Split on the working agreement's axis — how loudly a bug announces itself.

**Jacob writes** — silent failure, permanent, or first-instance-of-a-pattern:

| Piece | Why |
|---|---|
| `kernel/boot/start.S`, `linker.ld` entry change | The first instruction the kernel executes |
| CR0/CR4 setup, `FXSAVE`/`FXRSTOR` in `isr.S`, `#XF` handler | FPU corruption is silent and arbitrarily delayed |
| Limine module request and handoff | Bootloader contract |
| `kernel/wasm/runtime.c` — WAMR lifecycle | The integration, and the pattern later slices copy |
| `jani_log` and its linear-memory bounds check | First host function; sets house style for the entire syscall surface |

**Jacob writes** — loud failure, mechanical, spec-driven:

| Piece | Why |
|---|---|
| WAMR vendoring rule, Makefile integration, file list, flags | Build system; fails at link time |
| `kernel/wasm/shim/` and its hosted tests + fuzzer | Mechanical; a wrong `strcmp` fails loudly and is caught hosted |
| `kernel/wasm/platform/` | Adapter transcription against a documented API |
| `kernel/wasm/module_validate.zig` + tests + fuzzer | Zig trust-boundary validator |
| The test `.wasm` module | Tooling |
| Spec, plan, devlog, project documentation updates | Docs |

Jacob reviews everything, including Jacob's pieces.

## Risks

1. **libc shim scope.** The largest. Mitigated by hosted tests and by the shim
   being independently testable, but the estimate is soft.
2. **WAMR file-list churn.** Its internal dependencies may pull in more than the
   curated list. Annoying rather than dangerous — the linker names each one.
3. **Kernel stack depth.** WAMR's loader recurses over nested blocks on the
   native stack, and Limine's default stack is small. This surfaces as an
   unexplained triple fault with no output. **Measure the available stack on day
   one** rather than meet it by surprise.
4. **Vendoring needs network access,** which the development environment may
   lack. If so, the download step is Jacob's to run.
5. **A future SSE-using interrupt handler.** D3.1's `FXSAVE` choice removes this
   risk; noted only because the rejected alternative carried it.

## Open questions

None. All four decision points are resolved above.
