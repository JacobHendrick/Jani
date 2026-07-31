# WASM Interpreter Beachhead Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One `.wasm` module, delivered by Limine, executes inside the Jani kernel and prints a line over serial through a host function.

**Architecture:** Enable SSE kernel-wide with FPU state saved on interrupt (WASM needs floating point). Vendor WAMR's classic interpreter, hand it the kernel heap and a hand-written libc shim, and adapt its `os_*` platform surface onto `kmalloc`/`printk`/PIT ticks. Module bytes are structurally validated by a Zig module before WAMR sees them.

**Tech Stack:** C (zig cc, freestanding x86-64), Zig 0.16.0, WAMR classic interpreter, Limine boot protocol, QEMU.

**Spec:** `docs/superpowers/specs/2026-07-31-wasm-interpreter-beachhead-design.md`

## Global Constraints

- **Ownership is marked per task as [JACOB] or [JACOB].** The split follows the working agreement's failure-mode axis. Do not cross it without asking.
- **No comments in code written by Jacob.** Explain in chat, not in source.
- C style follows blueprint R5: declarations at the top of a block, explicit comparisons, no unchecked pointer arithmetic across object boundaries (R4).
- These must be green before every commit: `make test`, `make kernel`. `make crash-test` must be green before Tasks 1, 5, and 8 are considered done (they touch boot and interrupts).
- Commit at every green state (R7). Small commits.
- `LIMINE_COMMON_MAGIC` in this repo is `0xc7b1dd30df4c8b88, 0x0a82e883a194f07b` (see `kernel/mm/memory_map.c:6`). Limine base revision in use is 6.

## File Structure

| File | Responsibility | Owner |
|---|---|---|
| `kernel/boot/start.S` | Kernel entry; enables SSE before any C runs | JACOB |
| `kernel/boot/linker.ld` | `ENTRY(kmain)` → `ENTRY(_start)` | JACOB |
| `kernel/arch/isr.S` | Add `FXSAVE`/`FXRSTOR`, add `isr_simd_error` | JACOB |
| `kernel/arch/idt.c` | Register vector 19 | JACOB |
| `kernel/arch/interrupts.c` | Name vector 19 | JACOB |
| `kernel/wasm/shim/*.c,*.h` | The missing C library | JACOB |
| `kernel/wasm/platform/*.c,*.h` | WAMR `os_*` surface onto kernel services | JACOB |
| `kernel/wasm/module_validate.zig` | Structural validation of untrusted module bytes | JACOB |
| `kernel/wasm/runtime.c,.h` | WAMR lifecycle; registers the host function | JACOB |
| `kernel/boot/main.c` | Limine module request; calls the runtime | JACOB |
| `components/hello/hello.zig` | The test module | JACOB |
| `tools/hosted/test_wasm_shim.c` | Shim tests | JACOB |
| `tools/hosted/fuzz_wasm_shim.c` | Shim fuzzer | JACOB |
| `tools/hosted/test_wasm_module.c` | Validator tests | JACOB |
| `tools/hosted/fuzz_wasm_module.c` | Validator fuzzer | JACOB |

---

## Task 1 [JACOB]: Stack sizing and SSE/FPU enablement

Nothing in this slice compiles until this lands — a function returning `double` is a compile error under the current flags.

**Files:**
- Create: `kernel/boot/start.S`
- Modify: `kernel/boot/linker.ld:3`
- Modify: `kernel/arch/isr.S` (`isr_common`, plus a new stub)
- Modify: `kernel/arch/idt.c:66`
- Modify: `kernel/arch/interrupts.c:22`
- Modify: `kernel/boot/main.c` (stack request, float self-test)
- Modify: `Makefile` (drop `-mno-sse*`, drop the Zig `-mcpu` override, add `start.o`)
- Modify: `project documentation` (delete the now-wrong `grep -c xmm` instruction)

**Interfaces:**
- Produces: `_start` (kernel entry symbol, replaces `kmain` in the linker script). Nothing else consumes this task by name; everything consumes it by *being able to use floating point at all*.

- [ ] **Step 1: Measure the stack before anything else**

Add a Limine stack-size request to `main.c`. This removes the guess rather than measuring it — we ask for a known size instead of discovering the default.

```c
#define LIMINE_STACK_SIZE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x224ef0460a8e8926, 0xe1cb0fc25f46ea3d }

struct limine_stack_size_response {
    uint64_t revision;
};

struct limine_stack_size_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_stack_size_response *response;
    uint64_t stack_size;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST,
    .revision = 0,
    .response = 0,
    .stack_size = 256 * 1024
};
```

In `kmain`, print whether it was honored:

```c
    if (stack_size_request.response == 0) {
        kputs("WARNING: limine ignored the stack size request\n");
    } else {
        kputs("stack: 256 KiB requested and granted\n");
    }
```

> **Verify the request ID.** These two words come from the Limine protocol document, not from this repo — `third_party/limine/` ships binaries but no `limine.h`. If the ID is wrong, Limine silently ignores the request and `response` stays null, which the check above turns into a visible warning rather than a mystery. Confirm against the protocol doc for the vendored Limine version.

- [ ] **Step 2: Boot and confirm the stack line prints**

Run: `make iso && make run`
Expected: `stack: 256 KiB requested and granted` appears before the memory map.
If the warning prints instead, fix the request ID before continuing — everything after this depends on having stack headroom for WAMR's recursive loader.

- [ ] **Step 3: Commit the stack request**

```bash
git add kernel/boot/main.c
git commit -m "Request a 256 KiB boot stack from Limine"
```

- [ ] **Step 4: Write the entry stub**

Create `kernel/boot/start.S`:

```asm
.section .text

.global _start
.extern kmain

_start:
    movq %cr0, %rax
    andq $-13, %rax
    orq  $2, %rax
    movq %rax, %cr0

    movq %cr4, %rax
    orq  $0x600, %rax
    movq %rax, %cr4

    call kmain

halt_forever_asm:
    cli
    hlt
    jmp halt_forever_asm
```

`andq $-13` clears CR0.EM (bit 2, "emulate FPU") and CR0.TS (bit 3, "task switched", which would raise `#NM` on the first FP instruction). `orq $2` sets CR0.MP. `orq $0x600` sets CR4.OSFXSR (bit 9) and CR4.OSXMMEXCPT (bit 10).

This is assembly specifically because the code enabling SSE must not itself be compiled to use SSE.

- [ ] **Step 5: Point the linker at it**

In `kernel/boot/linker.ld` line 3, change `ENTRY(kmain)` to `ENTRY(_start)`.

- [ ] **Step 6: Add start.o to the build**

In `Makefile`, alongside the other object definitions:

```make
START_OBJ := $(BUILD_DIR)/start.o
START_SOURCE := kernel/boot/start.S
```

Add `$(START_OBJ)` to `KERNEL_OBJECTS` **first in the list**, before `$(MAIN_OBJ)`, and add the rule:

```make
$(START_OBJ): $(START_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(START_SOURCE) -o $(START_OBJ)
```

- [ ] **Step 7: Turn SSE back on in the build**

In `Makefile`, delete these three lines from `CFLAGS`:

```
	-mno-sse \
	-mno-sse2 \
	-mno-mmx \
```

And change `ZIG_KERNEL_TARGET` back to plain:

```make
ZIG_KERNEL_TARGET := -target x86_64-freestanding-none
```

- [ ] **Step 8: Save FPU state across interrupts**

In `kernel/arch/isr.S`, `isr_common` currently reads:

```asm
    cld
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call interrupt_handler
    movq %rbx, %rsp
```

Replace with:

```asm
    cld
    movq %rsp, %rdi
    movq %rsp, %rbx
    subq $512, %rsp
    andq $-16, %rsp
    fxsave (%rsp)
    movq %rsp, %r12
    call interrupt_handler
    fxrstor (%r12)
    movq %rbx, %rsp
```

Why this is correct: `%rdi` is captured before `%rsp` moves, so the handler still receives the register frame. `FXSAVE` needs a 16-byte-aligned 512-byte area, which is what `subq`/`andq` produce. `%r12` is callee-saved, so the C handler preserves it across the call; its guest value was already pushed and is restored by the `popq %r12` further down. The `call` pushes its return address *below* the save area, so nothing overwrites it.

- [ ] **Step 9: Handle SIMD floating-point exceptions**

Setting CR4.OSXMMEXCPT routes SIMD FP faults to vector 19. Without a handler they surface confusingly.

In `kernel/arch/isr.S`, following the exact shape of `isr_invalid_opcode`:

```asm
.global isr_simd_error

isr_simd_error:
    cli
    pushq $0
    pushq $19
    jmp isr_common
```

In `kernel/arch/idt.c`, after line 66:

```c
    idt_set_gate(19, (uint64_t)(uintptr_t)isr_simd_error, IDT_GATE_INTERRUPT);
```

and declare `isr_simd_error` alongside the other `extern` ISR declarations at the top of the file.

In `kernel/arch/interrupts.c`, in `exception_name`'s switch after the `case 6` arm:

```c
        case 19:
            return "simd floating-point exception";
```

- [ ] **Step 10: Add a floating-point self-test to main.c**

This is the proof the whole task worked. In `kmain`, after the heap test:

```c
    {
        volatile double a = 2.0;
        volatile double b = 3.5;
        volatile double product = a * b;

        if ((product > 6.9) && (product < 7.1)) {
            kputs("fpu: floating point works\n");
        } else {
            kputs("ERROR: floating point produced a wrong result\n");
        }
    }
```

`volatile` prevents the compiler from folding this at compile time, which would test nothing.

- [ ] **Step 11: Build and verify**

Run: `make kernel`
Expected: builds clean. Confirm SSE is now present:

```bash
objdump -d build/jani.elf | grep -c xmm
```

Expected: a non-zero count. Under the old flags this had to be 0; it is now expected to be large.

- [ ] **Step 12: Boot and check the whole existing system still works**

Run: `make iso && make run`

Expected serial output includes, in order:

```
stack: 256 KiB requested and granted
...
fpu: floating point works
heap bare-metal allocator test ok
virtio-blk ready: 16384 sectors of 512 bytes
...
object store on virtio-blk ok
tick: 1
```

The timer ticks are the critical part: they prove `FXSAVE`/`FXRSTOR` did not corrupt the interrupt path. If ticks stop or the kernel triple-faults, Step 8 is wrong.

- [ ] **Step 13: Run the full regression set**

```bash
make test
make crash-test
```

Expected: 1,003 hosted checks pass; crash test passes 25 cycles. The crash test matters here specifically because this task changed the interrupt path that the virtio driver's polling loop runs under.

- [ ] **Step 14: Remove the now-wrong instruction from project documentation**

Delete this paragraph, which is now actively misleading:

```
Kernel build: **no SSE in kernel code**, so ISRs never save XMM state. Note
that `-mgeneral-regs-only` is accepted and silently ignored on x86 by this
toolchain — the flags that work are `-mno-sse -mno-sse2 -mno-mmx`, plus
`-mcpu=x86_64-sse-sse2-mmx` for Zig. Check with `objdump -d build/jani.elf |
grep -c xmm`; it must print 0.
```

Replace with:

```
Kernel build: **SSE is on, and FPU state is saved on every interrupt**
(`FXSAVE`/`FXRSTOR` in `isr.S`). WASM needs floating point. SSE is enabled in
`kernel/boot/start.S` before any C runs, because the code that enables it must
not itself use it — that is why `linker.ld` says `ENTRY(_start)`, not
`ENTRY(kmain)`.
```

- [ ] **Step 15: Commit**

```bash
git add kernel/boot/start.S kernel/boot/linker.ld kernel/arch/isr.S \
        kernel/arch/idt.c kernel/arch/interrupts.c kernel/boot/main.c \
        Makefile project documentation
git commit -m "Enable SSE kernel-wide, save FPU state on interrupt"
```

---

## Task 2 [JACOB]: The libc shim, hosted-tested

**Files:**
- Create: `kernel/wasm/shim/string.c`, `stdlib.c`, `stdio.c`, `math.c`
- Create: `kernel/wasm/shim/include/{string.h,stdlib.h,stdio.h,assert.h,math.h,errno.h}`
- Create: `tools/hosted/test_wasm_shim.c`, `tools/hosted/fuzz_wasm_shim.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `kmalloc`/`kfree`/`krealloc` from `kernel/mm/heap.h`; `printk` from `kernel/lib/printk.h`; existing `memcpy`/`memset`/`memmove`/`strlen` from `kernel/lib/string.h`.
- Produces: the C standard functions listed below, with standard signatures, available to WAMR via `-I kernel/wasm/shim/include`.

Functions to provide (the four already in `kernel/lib/string.c` are re-exported by the header, not reimplemented):

| Header | Functions |
|---|---|
| `string.h` | `memcmp`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strchr`, `strrchr`, `strstr`, `strncpy`, plus re-exported `memcpy`/`memset`/`memmove`/`strlen` |
| `stdlib.h` | `malloc`, `free`, `realloc`, `calloc`, `abs`, `strtol`, `strtoul` |
| `stdio.h` | `snprintf`, `vsnprintf`, `printf`, `vprintf`, `putchar`, `puts` |
| `math.h` | `sqrt`, `fabs`, `ceil`, `floor`, `trunc`, `rint`, `fmin`, `fmax`, `copysign`, `isnan`, `isinf` (and `f`-suffixed variants) |
| `assert.h` | `assert` macro onto a kernel panic |
| `errno.h` | `errno` as a global, plus `EINVAL`/`ENOMEM`/`ERANGE` |

- [ ] **Step 1: Write failing tests for the risky function first**

`snprintf` is the reason this task has a test harness at all. Create `tools/hosted/test_wasm_shim.c`:

```c
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/wasm/shim/include/stdio.h"
#include "check.h"

unsigned long checks_passed;

static void test_snprintf_truncates_and_terminates(void) {
    char buffer[8];
    int written;

    written = jani_snprintf(buffer, sizeof(buffer), "%s", "abcdefghijkl");
    CHECK(written == 12);
    CHECK(buffer[7] == '\0');
    CHECK(buffer[0] == 'a');
    CHECK(buffer[6] == 'g');
}

static void test_snprintf_exact_fit(void) {
    char buffer[4];
    int written;

    written = jani_snprintf(buffer, sizeof(buffer), "%d", 123);
    CHECK(written == 3);
    CHECK(buffer[3] == '\0');
}

static void test_snprintf_zero_size_does_not_write(void) {
    char buffer[1];

    buffer[0] = 0x7F;
    CHECK(jani_snprintf(buffer, 0, "%d", 5) == 1);
    CHECK(buffer[0] == 0x7F);
}

int main(void) {
    test_snprintf_truncates_and_terminates();
    test_snprintf_exact_fit();
    test_snprintf_zero_size_does_not_write();
    printf("test_wasm_shim: %lu checks passed\n", checks_passed);
    return 0;
}
```

The return-value semantics tested here are the ones people get wrong: `snprintf` returns the length it *would* have written, not the length it did, and a size of 0 must not touch the buffer at all.

- [ ] **Step 2: Run to verify it fails**

Run: `make build/test_wasm_shim`
Expected: compile failure, no such file `kernel/wasm/shim/include/stdio.h`.

- [ ] **Step 3: Implement the shim**

Write the functions listed in the table. Implementation notes that matter:

- `snprintf`/`vsnprintf` take the format subset WAMR actually uses: `%d`, `%u`, `%x`, `%s`, `%c`, `%p`, `%lu`, `%llu`, `%f`. Anything else must not silently produce garbage — emit the literal characters.
- The kernel-facing names are `jani_snprintf` etc. with `#define snprintf jani_snprintf` in the header, so the hosted test can link against the host libc for `printf` while testing ours.
- `malloc`/`free`/`realloc` forward to `kmalloc`/`kfree`/`krealloc` in the kernel build, and to host `malloc` in the hosted build, selected by `#ifdef JANI_HOSTED`.
- Math functions compile to single instructions now that SSE is on: `sqrt` is `__builtin_sqrt`, `fabs` is `__builtin_fabs`, `floor`/`ceil`/`trunc`/`rint` are their `__builtin_` equivalents.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make build/test_wasm_shim && ./build/test_wasm_shim`
Expected: `test_wasm_shim: 9 checks passed`

- [ ] **Step 5: Add the fuzzer**

`tools/hosted/fuzz_wasm_shim.c` drives `jani_snprintf` with fuzzer-derived format strings and buffer sizes, asserting only that it never writes outside the buffer (ASan enforces this) and always terminates when size > 0. Wire `make fuzz-wasm-shim` following the existing `fuzz-heap` rule.

- [ ] **Step 6: Run the fuzzer**

Run: `make fuzz-wasm-shim`
Expected: clean campaign, no ASan reports.

- [ ] **Step 7: Commit**

```bash
git add kernel/wasm/shim tools/hosted/test_wasm_shim.c tools/hosted/fuzz_wasm_shim.c Makefile
git commit -m "libc shim for WAMR, with hosted tests and a snprintf fuzzer"
```

---

## Task 3 [JACOB]: Zig module pre-validator

**Files:**
- Create: `kernel/wasm/module_validate.zig`
- Create: `tools/hosted/test_wasm_module.c`, `tools/hosted/fuzz_wasm_module.c`
- Modify: `Makefile`

**Interfaces:**
- Produces: `int wasm_module_validate(const uint8_t *bytes, size_t length, uint32_t *section_count_out);` returning 1 for structurally valid, 0 otherwise. Task 8 consumes this exact signature.

Checks: the 4-byte magic `\0asm`; version == 1; each section's ID is known; the custom section (0) may repeat but all others must not; each section's LEB128 length stays inside the blob; total consumed length equals the blob length. It is explicitly **not** a semantic validator — WAMR does that.

- [ ] **Step 1: Write failing tests**

```c
static void test_rejects_bad_magic(void) {
    const uint8_t bytes[] = { 'X', 'a', 's', 'm', 1, 0, 0, 0 };
    uint32_t sections = 0;

    CHECK(!wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_truncated_section(void) {
    const uint8_t bytes[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x7F
    };
    uint32_t sections = 0;

    CHECK(!wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_accepts_minimal_empty_module(void) {
    const uint8_t bytes[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };
    uint32_t sections = 0;

    CHECK(wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 0);
}
```

The truncated-section case is the important one: section ID 1 declares length 0x7F but only 0 bytes follow.

- [ ] **Step 2: Run to verify it fails**

Run: `make build/test_wasm_module`
Expected: link failure, `wasm_module_validate` undefined.

- [ ] **Step 3: Implement in Zig**

`export fn wasm_module_validate(...) callconv(.c) c_int`, using slice bounds throughout so an overrun is a Zig panic in the hosted build rather than a silent read. Follow the structure of `kernel/obj/wal_validate.zig`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make build/test_wasm_module && ./build/test_wasm_module`
Expected: all checks pass.

- [ ] **Step 5: Add the fuzzer and run it**

`fuzz_wasm_module.c` feeds arbitrary bytes and asserts the validator returns without trapping. Run `make fuzz-wasm-module`; expected clean.

- [ ] **Step 6: Commit**

```bash
git add kernel/wasm/module_validate.zig tools/hosted/test_wasm_module.c \
        tools/hosted/fuzz_wasm_module.c Makefile
git commit -m "Zig structural validator for WASM module bytes"
```

---

## Task 4 [JACOB]: The test module

**Files:**
- Create: `components/hello/hello.zig`
- Modify: `Makefile`

**Interfaces:**
- Produces: `build/hello.wasm`, importing `jani_log(ptr: [*]const u8, len: u32) void` from module `"env"`, exporting `run() void`.

- [ ] **Step 1: Write the module**

```zig
extern "env" fn jani_log(ptr: [*]const u8, len: u32) void;

export fn run() void {
    const message = "hello from a WebAssembly module\n";
    jani_log(message.ptr, message.len);
}
```

- [ ] **Step 2: Add the build rule**

```make
HELLO_WASM := $(BUILD_DIR)/hello.wasm
HELLO_SOURCE := components/hello/hello.zig

$(HELLO_WASM): $(HELLO_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall \
	  -fno-entry --export=run $(HELLO_SOURCE) -femit-bin=$(HELLO_WASM)
```

- [ ] **Step 3: Build and inspect**

Run: `make build/hello.wasm && xxd build/hello.wasm | head -2`
Expected: first four bytes `0061 736d`, version `0100 0000`.

- [ ] **Step 4: Validate it with Task 3's validator**

Run the hosted validator against the real file to confirm the two agree.

- [ ] **Step 5: Commit**

```bash
git add components/hello Makefile
git commit -m "Test WASM module in Zig"
```

---

## Task 5 [JACOB]: Limine module delivery

**Files:**
- Modify: `kernel/boot/main.c`
- Modify: `kernel/boot/limine.conf`
- Modify: `Makefile` (copy the `.wasm` into the ISO tree)

**Interfaces:**
- Consumes: `wasm_module_validate` from Task 3; `build/hello.wasm` from Task 4.
- Produces: in `kmain`, a validated `{const uint8_t *bytes, size_t length}` pair passed to Task 8's `jani_wasm_run_module`.

- [ ] **Step 1: Declare the module request**

In `main.c`, beside the existing requests:

```c
#define LIMINE_MODULE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }

struct limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *cmdline;
    uint32_t media_type;
    uint32_t unused;
    uint32_t tftp_ip;
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    uint8_t gpt_disk_uuid[16];
    uint8_t gpt_part_uuid[16];
    uint8_t part_uuid[16];
};

struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;
};

struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .response = 0
};
```

> Same caveat as Task 1 Step 1: verify the request ID against the Limine protocol document for the vendored version. A wrong ID leaves `response` null, which Step 3 reports rather than hiding.

- [ ] **Step 2: Name the module in limine.conf**

Add to the kernel's boot entry in `kernel/boot/limine.conf`:

```
    module_path: boot():/boot/hello.wasm
```

And in the `Makefile`'s ISO rule, copy the module into the ISO tree beside the kernel:

```make
	cp $(HELLO_WASM) $(ISO_ROOT)/boot/hello.wasm
```

Add `$(HELLO_WASM)` to the ISO target's prerequisites.

- [ ] **Step 3: Fetch and validate the bytes**

In `kmain`, before the store demo:

```c
static int run_wasm_demo(void) {
    const uint8_t *module_bytes;
    size_t module_length;
    uint32_t section_count;

    if ((module_request.response == 0) ||
        (module_request.response->module_count == 0)) {
        kputs("ERROR: no wasm module supplied by the bootloader\n");
        return 0;
    }

    module_bytes = module_request.response->modules[0]->address;
    module_length = (size_t)module_request.response->modules[0]->size;
    printk("wasm: limine module, %d bytes\n", (int)module_length);

    section_count = 0;
    if (!wasm_module_validate(module_bytes, module_length, &section_count)) {
        kputs("ERROR: wasm module failed pre-validation\n");
        return 0;
    }
    printk("wasm: pre-validation ok, %d sections\n", (int)section_count);

    return 1;
}
```

Call it from `kmain` and report its result. Task 8 extends this function; for now it stops after validation.

- [ ] **Step 4: Boot and verify**

Run: `make iso && make run`

Expected:

```
wasm: limine module, 412 bytes
wasm: pre-validation ok, 6 sections
```

The byte count should match `ls -l build/hello.wasm` exactly. If it does not, the ISO is carrying a stale copy.

- [ ] **Step 5: Regression check**

```bash
make test
make crash-test
```

- [ ] **Step 6: Commit**

```bash
git add kernel/boot/main.c kernel/boot/limine.conf Makefile
git commit -m "Load the WASM module from Limine and pre-validate it"
```

---

> **Why Tasks 6 and 7 are less detailed than the rest.** Their steps cannot
> honestly be written yet: the curated file list and the exact `os_*` surface
> depend on the layout of WAMR's source tree, which nobody here has read
> because it is not vendored. Writing plausible-looking file lists from memory
> would be worse than admitting the gap. Both tasks are Jacob's; the detail
> gets filled in against the real tree at Step 1 of Task 6, before any of it is
> implemented.

## Task 6 [JACOB]: Vendor WAMR and integrate the build

**Files:**
- Modify: `Makefile`
- Create: `third_party/wamr/` (vendored, committed)

Follows the `tla2tools.jar` precedent exactly: pinned version, pinned sha256, download to `.tmp`, `sha256sum -c`, and only then `mv` into place. A failed check must never leave an artifact make would treat as valid.

Build configuration per spec D3.5: `WASM_ENABLE_INTERP=1`, `FAST_INTERP=0`, `AOT=0`, `JIT=0`, `LIBC_WASI=0`, `MULTI_MODULE=0`, `SHARED_MEMORY=0`, `THREAD_MGR=0`. No CMake — a curated `.c` list compiled with `zig cc` into `build/wamr/*.o`, with `-I kernel/wasm/shim/include` so Task 2's headers satisfy WAMR's includes.

- [x] **Step 1: Add the pinned download rule**
- [x] **Step 2: Verify the checksum and commit the vendored tree**
- [x] **Step 3: Add the curated file list and compile flags**
- [x] **Step 4: Build to a linkable archive; resolve missing symbols by extending the file list or Task 2's shim**
- [x] **Step 5: Commit**

> If the environment has no network access, Step 1 is Jacob's to run; everything after it is Jacob's.

**Done 2026-07-31 (commit `8bb4a9ed`).** Pinned `WAMR-2.4.5`, sha256
`1ab09d51099f276ca4a1d6629f6b589aab2bd0caa01445e05031a4bed22c199b`. Vendored
the curated subset — 26 compiled units, 75 files — with `make verify-wamr`
re-downloading the tarball and proving every vendored file matches upstream
byte for byte. Objects are linked into `KERNEL_OBJECTS` directly rather than
into an archive, so every undefined symbol must resolve now instead of at
Task 8.

Three corrections to this task as written:

- **`arch/invokeNative_em64.s` was missing from the plan's file list.** On
  x86-64 it is the hand-written trampoline that marshals wasm operands into
  SysV registers for a host call; `invokeNative_general.c` is documented
  upstream as unreliable on this target. Task 8 cannot call a host function
  without it. It is lowercase `.s`, so it needs its own flags — clang skips
  the preprocessor and rejects the C-only flags in `CFLAGS` under `-Werror`.
- **`../aot/aot_runtime.h` is included unguarded** by six files even with
  `WASM_ENABLE_AOT=0`, and it pulls `../compilation/aot.h`. Both are vendored
  as headers only; neither drags in LLVM.
- **`wasm_c_api.c` had to be vendored, not excluded.** `wasm_interp_classic.c`
  itself calls `wasm_runtime_invoke_c_api_native`, which needs
  `wasm_trap_delete`. A local stub would have been a fake.

`KERNEL_OBJECTS` is assigned with `:=` before this block exists, so the WAMR
objects are appended with `+=` afterwards. Referencing them inline in the
original assignment expands to nothing and silently links a WAMR-less kernel.

---

## Task 7 [JACOB]: WAMR platform port

**Files:**
- Create: `kernel/wasm/platform/platform_init.c`, `platform_internal.h`

**Interfaces:**
- Consumes: `kmalloc`/`kfree`/`krealloc`, `printk`, the PIT tick counter.
- Produces: WAMR's `os_*` surface. Consumed by Task 6's build, not called directly by any Jani code.

Maps `os_malloc`/`os_realloc`/`os_free` onto `k*`; `os_printf`/`os_vprintf` onto `printk`; `os_time_get_boot_us` onto PIT ticks; `bh_platform_init` returns 0. Mutex, condition-variable, and thread functions are no-ops — legitimate because `THREAD_MGR=0` and I5 makes components single-threaded by definition.

- [x] **Step 1: Implement the surface**
- [x] **Step 2: Link the kernel and resolve remaining undefined symbols**
- [x] **Step 3: `make kernel` green**
- [x] **Step 4: Commit**

**Done 2026-07-31 (commit `8bb4a9ed`).** `platform_internal.h` supplies the
types WAMR expects (`korp_mutex`, `korp_cond`, `korp_tid`, `os_file_handle`,
`os_getpagesize`), `platform_init.c` the functions. Mutex and condition
variables are no-ops per I5. `os_dumps_proc_mem_info` was needed too —
`bh_log.c` calls it unguarded.

Two decisions worth carrying into Task 8:

- **`os_thread_get_stack_boundary` returns NULL.** Upstream explicitly allows
  this and branches on it, but the consequence is that WAMR cannot check for
  native stack overflow. A deeply recursive module is bounded only by WAMR's
  own wasm stack limit, not by the 256 KiB boot stack. This is the largest
  known gap in the port and wants a real answer before untrusted modules run.
- **`os_mmap` returns NULL and `os_mprotect` returns -1** rather than faking
  success. Nothing in the interpreter-only path calls them —
  `OS_ENABLE_HW_BOUND_CHECK` is off, so WAMR emits explicit bounds
  comparisons instead of the guard-page-plus-SIGSEGV trick, which a kernel
  cannot do anyway. If a path ever does reach them, it fails loudly.

`os_time_get_boot_us` is PIT ticks at the 100 Hz `main.c:504` sets, so its
resolution is 10 ms. Fine for logging, wrong for anything metering U9.

---

## Task 8 [JACOB]: WAMR lifecycle and the host function

The payoff task.

**Files:**
- Create: `kernel/wasm/runtime.c`, `kernel/wasm/runtime.h`
- Modify: `kernel/boot/main.c` (extend `run_wasm_demo` from Task 5)
- Modify: `Makefile`

**Interfaces:**
- Consumes: WAMR's API from Task 6; the platform port from Task 7; validated bytes from Task 5.
- Produces: `int jani_wasm_run_module(const uint8_t *bytes, size_t length);` returning 1 on success.

- [ ] **Step 1: Declare the interface**

`kernel/wasm/runtime.h`:

```c
#ifndef JANI_KERNEL_WASM_RUNTIME_H
#define JANI_KERNEL_WASM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

int jani_wasm_run_module(const uint8_t *bytes, size_t length);

#endif
```

- [ ] **Step 2: Write the host function**

This is the first host function in the project and it sets the shape every syscall copies later. The bounds check is the whole point — `offset` and `length` arrive from inside the sandbox and must be validated against the instance's linear memory before any pointer is formed (R4).

```c
static void jani_log_wrapper(
    wasm_exec_env_t exec_env,
    uint32_t offset,
    uint32_t length
) {
    wasm_module_inst_t instance;
    char *text;
    uint32_t index;

    instance = wasm_runtime_get_module_inst(exec_env);

    if (!wasm_runtime_validate_app_addr(instance, offset, length)) {
        kputs("ERROR: wasm log call out of bounds\n");
        return;
    }

    text = (char *)wasm_runtime_addr_app_to_native(instance, offset);
    for (index = 0; index < length; index++) {
        printk("%c", text[index]);
    }
}
```

`wasm_runtime_validate_app_addr` is WAMR's own bounds check against the instance's memory; using it rather than hand-rolling the comparison is deliberate, because WAMR knows about memory growth and we do not.

- [ ] **Step 3: Register it and run the module**

```c
static NativeSymbol native_symbols[] = {
    { "jani_log", jani_log_wrapper, "(ii)", NULL }
};
```

Then in `jani_wasm_run_module`: `wasm_runtime_full_init` with the allocator pointed at `kmalloc`/`krealloc`/`kfree`, `wasm_runtime_register_natives("env", ...)`, `wasm_runtime_load`, `wasm_runtime_instantiate`, `wasm_runtime_lookup_function` for `"run"`, `wasm_runtime_call_wasm`, then deinstantiate/unload/destroy.

Print one line per step so a failure names itself:

```c
    kputs("wamr: runtime initialized\n");
    printk("wamr: module loaded, %d exports\n", (int)export_count);
    printk("wamr: instance created, %d memory pages\n", (int)pages);
    ...
    kputs("wamr: instance destroyed, heap returned\n");
```

- [ ] **Step 4: Call it from main.c**

Extend `run_wasm_demo` so that after successful pre-validation it calls `jani_wasm_run_module(module_bytes, module_length)` and reports the result.

- [ ] **Step 5: Build and run**

Run: `make iso && make run`

Expected:

```
wasm: limine module, 412 bytes
wasm: pre-validation ok, 6 sections
wamr: runtime initialized
wamr: module loaded, 1 exports
wamr: instance created, 1 memory pages
hello from a WebAssembly module
wamr: instance destroyed, heap returned
```

- [ ] **Step 6: Check the heap actually came back**

Print `kheap_used_bytes()` before and after. They should match. A mismatch means WAMR leaked, which matters because slice 2 instantiates components repeatedly.

- [ ] **Step 7: Full regression**

```bash
make test
make crash-test
```

- [ ] **Step 8: Commit**

```bash
git add kernel/wasm/runtime.c kernel/wasm/runtime.h kernel/boot/main.c Makefile
git commit -m "Run a WASM module in the kernel through WAMR"
```

---

## Definition of done

- Serial log shows `hello from a WebAssembly module` produced by the module itself.
- `make test`, `make kernel`, `make crash-test` all green.
- Heap usage returns to its pre-run value after teardown.
- Devlog entry written; project documentation current-state section updated.
