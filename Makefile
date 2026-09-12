ZIG := third_party/zig/zig
CC := $(ZIG) cc
LD := ld
QEMU := qemu-system-x86_64
XORRISO := xorriso
QEMU_FLAGS := -cpu qemu64,-apic

# Hosted twin (blueprint rule R3): kernel allocator code compiled as normal
# Linux binaries under sanitizers. clang, not zig cc: zig does not ship the
# ASan runtime for host targets (verified), and clang bundles libFuzzer.
HOST_CC := clang
HOST_CFLAGS := -Wall -Wextra -Werror -g -O1 \
	-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie
FUZZ_CFLAGS := $(HOST_CFLAGS) -fsanitize=fuzzer
FUZZ_RUNS ?= 10000
CRASH_CYCLES ?= 25
WOW_CYCLES ?= 3
WOW_SECONDS ?= 40

BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
RESUME_ISO_ROOT := $(BUILD_DIR)/iso_resume_root
ZIG_GLOBAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-global-cache)
ZIG_LOCAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-local-cache)
ZIG_ENV := env ZIG_GLOBAL_CACHE_DIR=$(ZIG_GLOBAL_CACHE_DIR) ZIG_LOCAL_CACHE_DIR=$(ZIG_LOCAL_CACHE_DIR)

KERNEL_ELF := $(BUILD_DIR)/jani.elf
ISO_IMAGE := $(BUILD_DIR)/jani.iso
RESUME_ISO_IMAGE := $(BUILD_DIR)/jani-resume.iso
START_OBJ := $(BUILD_DIR)/start.o
MAIN_OBJ := $(BUILD_DIR)/main.o
GDT_OBJ := $(BUILD_DIR)/gdt.o
STACK_OBJ := $(BUILD_DIR)/stack.o
IDT_OBJ := $(BUILD_DIR)/idt.o
ISR_OBJ := $(BUILD_DIR)/isr.o
INTERRUPTS_OBJ := $(BUILD_DIR)/interrupts.o
PIC_OBJ := $(BUILD_DIR)/pic.o
PIT_OBJ := $(BUILD_DIR)/pit.o
KEYBOARD_OBJ := $(BUILD_DIR)/keyboard.o
MEMORY_MAP_OBJ := $(BUILD_DIR)/memory_map.o
LAYOUT_OBJ := $(BUILD_DIR)/layout.o
PMM_OBJ := $(BUILD_DIR)/pmm.o
VMM_OBJ := $(BUILD_DIR)/vmm.o
MMIO_OBJ := $(BUILD_DIR)/mmio.o
HEAP_OBJ := $(BUILD_DIR)/heap.o
FREE_LIST_OBJ := $(BUILD_DIR)/free_list.o
HEAP_BACKEND_OBJ := $(BUILD_DIR)/heap_backend.o
OBJECT_ID_OBJ := $(BUILD_DIR)/object_id.o
CAPABILITY_OBJ := $(BUILD_DIR)/capability.o
CAP_TABLE_OBJ := $(BUILD_DIR)/cap_table.o
DERIVATION_OBJ := $(BUILD_DIR)/derivation.o
OBJECT_TABLE_OBJ := $(BUILD_DIR)/object_table.o
OBJECT_HEADER_VALIDATE_OBJ := $(BUILD_DIR)/object_header_validate.o
WAL_VALIDATE_OBJ := $(BUILD_DIR)/wal_validate.o
OBJECT_STORE_OBJ := $(BUILD_DIR)/object_store.o
PCI_OBJ := $(BUILD_DIR)/pci.o
VIRTIO_PCI_OBJ := $(BUILD_DIR)/virtio_pci.o
VIRTQUEUE_OBJ := $(BUILD_DIR)/virtqueue.o
VIRTIO_BLK_OBJ := $(BUILD_DIR)/virtio_blk.o
SERIAL_OBJ := $(BUILD_DIR)/serial.o
PRINTK_OBJ := $(BUILD_DIR)/printk.o
STRING_OBJ := $(BUILD_DIR)/string.o
KERNEL_OBJECTS := $(START_OBJ) $(MAIN_OBJ) $(GDT_OBJ) $(STACK_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(INTERRUPTS_OBJ) $(PIC_OBJ) $(PIT_OBJ) $(KEYBOARD_OBJ) $(MEMORY_MAP_OBJ) $(LAYOUT_OBJ) $(PMM_OBJ) $(VMM_OBJ) $(MMIO_OBJ) $(HEAP_OBJ) $(FREE_LIST_OBJ) $(HEAP_BACKEND_OBJ) $(OBJECT_ID_OBJ) $(CAPABILITY_OBJ) $(CAP_TABLE_OBJ) $(DERIVATION_OBJ) $(OBJECT_TABLE_OBJ) $(OBJECT_HEADER_VALIDATE_OBJ) $(WAL_VALIDATE_OBJ) $(OBJECT_STORE_OBJ) $(PCI_OBJ) $(VIRTIO_PCI_OBJ) $(VIRTQUEUE_OBJ) $(VIRTIO_BLK_OBJ) $(SERIAL_OBJ) $(PRINTK_OBJ) $(STRING_OBJ)

START_SOURCE := kernel/boot/start.S
KERNEL_SOURCE := kernel/boot/main.c
GDT_SOURCE := kernel/arch/gdt.c
STACK_SOURCE := kernel/arch/stack.c
IDT_SOURCE := kernel/arch/idt.c
ISR_SOURCE := kernel/arch/isr.S
INTERRUPTS_SOURCE := kernel/arch/interrupts.c
PIC_SOURCE := kernel/drivers/pic.c
PIT_SOURCE := kernel/drivers/pit.c
KEYBOARD_SOURCE := kernel/drivers/keyboard.c
MEMORY_MAP_SOURCE := kernel/mm/memory_map.c
LAYOUT_SOURCE := kernel/mm/layout.c
PMM_SOURCE := kernel/mm/pmm.c
VMM_SOURCE := kernel/mm/vmm.c
MMIO_SOURCE := kernel/mm/mmio.c
HEAP_SOURCE := kernel/mm/heap.c
FREE_LIST_SOURCE := kernel/mm/free_list.c
HEAP_BACKEND_SOURCE := kernel/mm/heap_backend.c
OBJECT_ID_SOURCE := kernel/obj/object_id.c
CAPABILITY_SOURCE := kernel/cap/capability.c
CAP_TABLE_SOURCE := kernel/cap/cap_table.c
DERIVATION_SOURCE := kernel/cap/derivation.c
OBJECT_TABLE_SOURCE := kernel/obj/object_table.c
OBJECT_HEADER_VALIDATE_SOURCE := kernel/obj/object_header_validate.zig
WAL_VALIDATE_SOURCE := kernel/obj/wal_validate.zig
OBJECT_STORE_SOURCE := kernel/obj/object_store.c
PCI_SOURCE := kernel/drivers/pci.c
VIRTIO_PCI_SOURCE := kernel/drivers/virtio_pci.c
VIRTQUEUE_SOURCE := kernel/drivers/virtqueue.c
VIRTIO_BLK_SOURCE := kernel/drivers/virtio_blk.c
SERIAL_SOURCE := kernel/drivers/serial.c
PRINTK_SOURCE := kernel/lib/printk.c
STRING_SOURCE := kernel/lib/string.c
HELLO_WASM := $(BUILD_DIR)/hello.wasm
HELLO_SOURCE := components/hello/hello.zig
COUNTER_WASM := $(BUILD_DIR)/counter.wasm
COUNTER_SOURCE := components/counter/counter.zig
SDK_ZIG := sdk/zig/jani.zig

LINKER_SCRIPT := kernel/boot/linker.ld
LIMINE_CONFIG := kernel/boot/limine.conf
LIMINE_RESUME_CONFIG := kernel/boot/limine-resume.conf
LIMINE_DIR := third_party/limine

CFLAGS := -target x86_64-freestanding-none \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pie \
	-mcmodel=kernel \
	-mno-red-zone \
	-Wall \
	-Wextra \
	-Werror \
	-O0 \
	-g \
	-fsanitize=undefined \
	-fsanitize-trap=undefined \
	$(EXTRA_CFLAGS)

EXTRA_CFLAGS ?=

ZIG_KERNEL_TARGET := -target x86_64-freestanding-none

LDFLAGS := -T $(LINKER_SCRIPT)

HOSTED_DIR := tools/hosted
TEST_PMM_BIN := $(BUILD_DIR)/test_pmm
TEST_HEAP_BIN := $(BUILD_DIR)/test_heap
TEST_OBJECT_TABLE_BIN := $(BUILD_DIR)/test_object_table
TEST_CAPABILITY_BIN := $(BUILD_DIR)/test_capability
TEST_DERIVATION_BIN := $(BUILD_DIR)/test_derivation
TEST_OBJECT_HEADER_BIN := $(BUILD_DIR)/test_object_header
TEST_WAL_BIN := $(BUILD_DIR)/test_wal
TEST_OBJECT_STORE_BIN := $(BUILD_DIR)/test_object_store
TEST_WRITE_ORDERING_BIN := $(BUILD_DIR)/test_write_ordering
TEST_WASM_SHIM_BIN := $(BUILD_DIR)/test_wasm_shim
TEST_WASM_MODULE_BIN := $(BUILD_DIR)/test_wasm_module
MODULE_VALIDATE_HOSTED_OBJ := $(BUILD_DIR)/module_validate_hosted.o
OBJECT_HEADER_VALIDATE_HOSTED_OBJ := $(BUILD_DIR)/object_header_validate_hosted.o
WAL_VALIDATE_HOSTED_OBJ := $(BUILD_DIR)/wal_validate_hosted.o
FUZZ_HEAP_BIN := $(BUILD_DIR)/fuzz_heap
FUZZ_OBJECT_STORE_BIN := $(BUILD_DIR)/fuzz_object_store
FUZZ_WASM_SHIM_BIN := $(BUILD_DIR)/fuzz_wasm_shim
FUZZ_WASM_MODULE_BIN := $(BUILD_DIR)/fuzz_wasm_module
TEST_SYSCALL_ARGS_BIN := $(BUILD_DIR)/test_syscall_args
TEST_DETERMINISM_BIN := $(BUILD_DIR)/test_determinism
TEST_COMPONENT_STATE_BIN := $(BUILD_DIR)/test_component_state
TEST_COMPONENT_SET_BIN := $(BUILD_DIR)/test_component_set
TEST_COMPONENT_MAILBOX_BIN := $(BUILD_DIR)/test_component_mailbox
TEST_COMPONENT_UNINSTALL_BIN := $(BUILD_DIR)/test_component_uninstall
TEST_IDLC_BIN := $(BUILD_DIR)/test_idlc
IDLC_BIN := $(BUILD_DIR)/idlc
IDLC_DIR := tools/idlc
IDL_SYSCALLS := idl/syscalls.idl
IDL_RECORDS := idl/records.idl
GENERATED_DIR := kernel/wasm/generated
IDLC_SOURCES := $(IDLC_DIR)/lexer.c $(IDLC_DIR)/parser.c $(IDLC_DIR)/emit_table.c $(IDLC_DIR)/emit_zig.c $(IDLC_DIR)/emit_c.c $(IDLC_DIR)/emit_conform.c
IDLC_HEADERS := $(IDLC_DIR)/lexer.h $(IDLC_DIR)/parser.h $(IDLC_DIR)/ast.h $(IDLC_DIR)/emit.h
FUZZ_SYSCALL_ARGS_BIN := $(BUILD_DIR)/fuzz_syscall_args
SYSCALL_ARGS_SOURCE := kernel/wasm/syscall_args.zig
SYSCALL_ARGS_OBJ := $(BUILD_DIR)/syscall_args.o
SYSCALL_ARGS_HOSTED_OBJ := $(BUILD_DIR)/syscall_args_hosted.o
MODULE_VALIDATE_SOURCE := kernel/wasm/module_validate.zig
MODULE_VALIDATE_OBJ := $(BUILD_DIR)/module_validate.o
WASM_SHIM_SOURCES := kernel/wasm/shim/string.c kernel/wasm/shim/stdio.c \
	kernel/wasm/shim/stdlib.c kernel/wasm/shim/math.c
WASM_SHIM_OBJECTS := $(BUILD_DIR)/shim_string.o $(BUILD_DIR)/shim_stdio.o \
	$(BUILD_DIR)/shim_stdlib.o $(BUILD_DIR)/shim_math.o

# --- WAMR (Phase 3 slice 1, spec D3.5) ---------------------------------------
# Only the files the interpreter-only configuration actually compiles are
# vendored, so the wildcards below are the file list. `make verify-wamr`
# re-downloads the pinned tarball and proves every vendored byte matches it.
WAMR_DIR := third_party/wamr
WAMR_VERSION := WAMR-2.4.5
WAMR_URL := https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/$(WAMR_VERSION).tar.gz
WAMR_SHA256 := 1ab09d51099f276ca4a1d6629f6b589aab2bd0caa01445e05031a4bed22c199b

WAMR_COMMON_DIR := $(WAMR_DIR)/core/iwasm/common
WAMR_INTERP_DIR := $(WAMR_DIR)/core/iwasm/interpreter
WAMR_UTILS_DIR := $(WAMR_DIR)/core/shared/utils
WAMR_MEMALLOC_DIR := $(WAMR_DIR)/core/shared/mem-alloc
WAMR_EMS_DIR := $(WAMR_DIR)/core/shared/mem-alloc/ems
WAMR_PLATFORM_DIR := kernel/wasm/platform

WAMR_C_SOURCES := $(wildcard $(WAMR_COMMON_DIR)/*.c) \
	$(wildcard $(WAMR_INTERP_DIR)/*.c) \
	$(wildcard $(WAMR_UTILS_DIR)/*.c) \
	$(wildcard $(WAMR_MEMALLOC_DIR)/*.c) \
	$(wildcard $(WAMR_EMS_DIR)/*.c)

WAMR_OBJECTS := $(addprefix $(BUILD_DIR)/wamr/,$(notdir $(WAMR_C_SOURCES:.c=.o))) \
	$(BUILD_DIR)/wamr/invokeNative_em64.o
WAMR_PLATFORM_OBJ := $(BUILD_DIR)/wamr/platform_init.o

WAMR_INCLUDES := -I$(WAMR_DIR)/core/shared/platform/include \
	-I$(WAMR_PLATFORM_DIR) \
	-I$(WAMR_UTILS_DIR) \
	-I$(WAMR_DIR)/core/iwasm/include \
	-I$(WAMR_COMMON_DIR) \
	-I$(WAMR_INTERP_DIR) \
	-I$(WAMR_MEMALLOC_DIR) \
	-Ikernel/wasm/shim/include

WAMR_DEFINES := -DWASM_ENABLE_INTERP=1 \
	-DWASM_ENABLE_INSTRUCTION_METERING=1 \
	-DBH_MALLOC=wasm_runtime_malloc \
	-DBH_FREE=wasm_runtime_free \
	-DBH_PLATFORM_JANI

# -Werror stays on so a genuinely new warning in vendored code still stops the
# build; the two suppressed categories are dead parameters left behind by the
# feature flags we turn off, and appear in upstream as shipped.
WAMR_CFLAGS := $(CFLAGS) -Wno-unused-parameter -Wno-unused-variable

# WAMR puts first_table at global_data + global_data_size, rarely 8-aligned,
# then stores 8-aligned fields through it. Benign on x86-64 at -O0; traps under
# -fsanitize=alignment. Only this check, only vendored objects: the loader eats
# untrusted bytes, so the rest of UBSan stays. Revisit if we leave -O0.
WAMR_CFLAGS += -fno-sanitize=alignment

# invokeNative_em64.s is lowercase .s, so clang does not run the preprocessor
# and rejects the C-only flags in CFLAGS as unused under -Werror. It is the
# hand-written SysV trampoline that marshals wasm operands into registers for
# a host call, so it must be assembled, not skipped.
WAMR_ASFLAGS := -target x86_64-freestanding-none -g

WASM_RUNTIME_OBJ := $(BUILD_DIR)/wasm_runtime.o
WASM_RUNTIME_SOURCE := kernel/wasm/runtime.c
SYSCALLS_OBJ := $(BUILD_DIR)/syscalls.o
SYSCALLS_SOURCE := kernel/wasm/syscalls.c
COMPONENT_OBJ := $(BUILD_DIR)/component.o
COMPONENT_SOURCE := kernel/wasm/component.c
COMPONENT_SET_OBJ := $(BUILD_DIR)/component_set.o
COMPONENT_SET_SOURCE := kernel/wasm/component_set.c
INSTANCE_STATE_OBJ := $(BUILD_DIR)/instance_state.o
INSTANCE_STATE_SOURCE := kernel/wasm/instance_state.c

# Appended, not folded into the KERNEL_OBJECTS assignment above: that uses :=
# and is evaluated before this block exists, so an inline reference there
# expands to nothing and links a WAMR-less kernel without complaining.
KERNEL_OBJECTS += $(WASM_SHIM_OBJECTS) $(WAMR_PLATFORM_OBJ) $(WAMR_OBJECTS) \
	$(MODULE_VALIDATE_OBJ) $(SYSCALL_ARGS_OBJ) $(WASM_RUNTIME_OBJ) \
	$(COMPONENT_OBJ) $(COMPONENT_SET_OBJ) $(INSTANCE_STATE_OBJ) \
	$(SYSCALLS_OBJ)

.DEFAULT_GOAL := all
include kernel/net/net.mk
include kernel/phase4.mk

.PHONY: all check-tools kernel iso resume-iso run run-debug test fuzz-heap \
	fuzz-object-store fuzz-wasm-shim fuzz-wasm-module fuzz-syscall-args \
	model-check model-check-negative \
write-ordering-negative crash-test wow-demo wow-demo-negative zero-install-test \
hello-wasm counter-wasm \
	verify-wamr clean \
	idlc idl-generate idl-check idl-negative

all: iso

check-tools:
	$(ZIG) version
	$(LD) --version
	objcopy --version
	$(XORRISO) -version
	$(QEMU) --version
	$(LIMINE_DIR)/limine --version

$(START_OBJ): $(START_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(START_SOURCE) -o $(START_OBJ)

$(MAIN_OBJ): $(KERNEL_SOURCE) kernel/wasm/component.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(KERNEL_SOURCE) -o $(MAIN_OBJ)

$(GDT_OBJ): $(GDT_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(GDT_SOURCE) -o $(GDT_OBJ)

$(IDT_OBJ): $(IDT_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(IDT_SOURCE) -o $(IDT_OBJ)

$(ISR_OBJ): $(ISR_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(ISR_SOURCE) -o $(ISR_OBJ)

$(INTERRUPTS_OBJ): $(INTERRUPTS_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(INTERRUPTS_SOURCE) -o $(INTERRUPTS_OBJ)

$(PIC_OBJ): $(PIC_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PIC_SOURCE) -o $(PIC_OBJ)

$(PIT_OBJ): $(PIT_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PIT_SOURCE) -o $(PIT_OBJ)

$(KEYBOARD_OBJ): $(KEYBOARD_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(KEYBOARD_SOURCE) -o $(KEYBOARD_OBJ)

$(MEMORY_MAP_OBJ): $(MEMORY_MAP_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(MEMORY_MAP_SOURCE) -o $(MEMORY_MAP_OBJ)

$(LAYOUT_OBJ): $(LAYOUT_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(LAYOUT_SOURCE) -o $(LAYOUT_OBJ)

$(MMIO_OBJ): $(MMIO_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(MMIO_SOURCE) -o $(MMIO_OBJ)

$(PCI_OBJ): $(PCI_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PCI_SOURCE) -o $(PCI_OBJ)

$(VIRTIO_PCI_OBJ): $(VIRTIO_PCI_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VIRTIO_PCI_SOURCE) -o $(VIRTIO_PCI_OBJ)

$(VIRTQUEUE_OBJ): $(VIRTQUEUE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VIRTQUEUE_SOURCE) -o $(VIRTQUEUE_OBJ)

$(VIRTIO_BLK_OBJ): $(VIRTIO_BLK_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VIRTIO_BLK_SOURCE) -o $(VIRTIO_BLK_OBJ)

$(PMM_OBJ): $(PMM_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PMM_SOURCE) -o $(PMM_OBJ)

$(VMM_OBJ): $(VMM_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VMM_SOURCE) -o $(VMM_OBJ)

$(HEAP_OBJ): $(HEAP_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(HEAP_SOURCE) -o $(HEAP_OBJ)

$(FREE_LIST_OBJ): $(FREE_LIST_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(FREE_LIST_SOURCE) -o $(FREE_LIST_OBJ)

$(HEAP_BACKEND_OBJ): $(HEAP_BACKEND_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(HEAP_BACKEND_SOURCE) -o $(HEAP_BACKEND_OBJ)

$(OBJECT_ID_OBJ): $(OBJECT_ID_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(OBJECT_ID_SOURCE) -o $(OBJECT_ID_OBJ)

$(CAPABILITY_OBJ): $(CAPABILITY_SOURCE) kernel/cap/capability.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(CAPABILITY_SOURCE) -o $(CAPABILITY_OBJ)

$(CAP_TABLE_OBJ): $(CAP_TABLE_SOURCE) kernel/cap/cap_table.h kernel/cap/capability.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(CAP_TABLE_SOURCE) -o $(CAP_TABLE_OBJ)

$(DERIVATION_OBJ): $(DERIVATION_SOURCE) kernel/cap/derivation.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(DERIVATION_SOURCE) -o $(DERIVATION_OBJ)

$(OBJECT_TABLE_OBJ): $(OBJECT_TABLE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(OBJECT_TABLE_SOURCE) -o $(OBJECT_TABLE_OBJ)

$(OBJECT_HEADER_VALIDATE_OBJ): $(OBJECT_HEADER_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O Debug $(OBJECT_HEADER_VALIDATE_SOURCE) -femit-bin=$(OBJECT_HEADER_VALIDATE_OBJ)

$(WAL_VALIDATE_OBJ): $(WAL_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O Debug $(WAL_VALIDATE_SOURCE) -femit-bin=$(WAL_VALIDATE_OBJ)

$(OBJECT_STORE_OBJ): $(OBJECT_STORE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(OBJECT_STORE_SOURCE) -o $(OBJECT_STORE_OBJ)

$(SERIAL_OBJ): $(SERIAL_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(SERIAL_SOURCE) -o $(SERIAL_OBJ)

$(PRINTK_OBJ): $(PRINTK_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PRINTK_SOURCE) -o $(PRINTK_OBJ)

$(STRING_OBJ): $(STRING_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(STRING_SOURCE) -o $(STRING_OBJ)

$(STACK_OBJ): $(STACK_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(STACK_SOURCE) -o $(STACK_OBJ)

$(BUILD_DIR)/shim_string.o: kernel/wasm/shim/string.c Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c kernel/wasm/shim/string.c -o $@

$(BUILD_DIR)/shim_stdio.o: kernel/wasm/shim/stdio.c Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c kernel/wasm/shim/stdio.c -o $@

$(BUILD_DIR)/shim_stdlib.o: kernel/wasm/shim/stdlib.c Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c kernel/wasm/shim/stdlib.c -o $@

$(BUILD_DIR)/shim_math.o: kernel/wasm/shim/math.c Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c kernel/wasm/shim/math.c -o $@

$(BUILD_DIR)/wamr/%.o: $(WAMR_COMMON_DIR)/%.c Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

$(BUILD_DIR)/wamr/%.o: $(WAMR_INTERP_DIR)/%.c Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

$(BUILD_DIR)/wamr/%.o: $(WAMR_UTILS_DIR)/%.c Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

$(BUILD_DIR)/wamr/%.o: $(WAMR_MEMALLOC_DIR)/%.c Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

$(BUILD_DIR)/wamr/%.o: $(WAMR_EMS_DIR)/%.c Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

$(BUILD_DIR)/wamr/invokeNative_em64.o: $(WAMR_COMMON_DIR)/arch/invokeNative_em64.s Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(WAMR_ASFLAGS) -c $< -o $@

$(WAMR_PLATFORM_OBJ): $(WAMR_PLATFORM_DIR)/platform_init.c $(WAMR_PLATFORM_DIR)/platform_internal.h Makefile
	mkdir -p $(BUILD_DIR)/wamr $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) $(WAMR_DEFINES) $(WAMR_INCLUDES) -c $< -o $@

verify-wamr:
	rm -rf $(BUILD_DIR)/wamr-verify
	mkdir -p $(BUILD_DIR)/wamr-verify
	curl -fL -o $(BUILD_DIR)/wamr-verify/wamr.tar.gz $(WAMR_URL)
	echo "$(WAMR_SHA256)  $(BUILD_DIR)/wamr-verify/wamr.tar.gz" | sha256sum -c -
	tar xzf $(BUILD_DIR)/wamr-verify/wamr.tar.gz -C $(BUILD_DIR)/wamr-verify
	@set -e; count=0; \
	upstream=$(BUILD_DIR)/wamr-verify/wasm-micro-runtime-$(WAMR_VERSION); \
	for f in $$(cd $(WAMR_DIR) && find . -type f | sort); do \
	  if [ ! -f "$$upstream/$$f" ]; then \
	    echo "verify-wamr: $$f is not in $(WAMR_VERSION)"; exit 1; fi; \
	  cmp -s "$(WAMR_DIR)/$$f" "$$upstream/$$f" || \
	    { echo "verify-wamr: $$f differs from upstream"; exit 1; }; \
	  count=$$((count + 1)); \
	done; \
	echo "verify-wamr: $$count vendored files match $(WAMR_VERSION) byte for byte"

$(KERNEL_ELF): $(KERNEL_OBJECTS) $(LINKER_SCRIPT)
	mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $(KERNEL_ELF)

kernel: $(KERNEL_ELF)

iso: $(ISO_IMAGE)

resume-iso: $(RESUME_ISO_IMAGE)

$(ISO_IMAGE): $(KERNEL_ELF) $(LIMINE_CONFIG) $(COUNTER_WASM)
	mkdir -p $(ISO_ROOT)/boot
	mkdir -p $(ISO_ROOT)/boot/limine
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/jani.elf
	cp $(COUNTER_WASM) $(ISO_ROOT)/boot/counter.wasm
	cp $(LIMINE_CONFIG) $(ISO_ROOT)/boot/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	$(XORRISO) -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part \
		--efi-boot-image \
		--protective-msdos-label \
		$(ISO_ROOT) \
		-o $(ISO_IMAGE)
	$(LIMINE_DIR)/limine bios-install $(ISO_IMAGE)

# This ISO deliberately contains no Wasm module. It can only resume a component
# that was installed into the persistent object store by an earlier boot.
$(RESUME_ISO_IMAGE): $(KERNEL_ELF) $(LIMINE_RESUME_CONFIG)
	rm -rf $(RESUME_ISO_ROOT)
	mkdir -p $(RESUME_ISO_ROOT)/boot
	mkdir -p $(RESUME_ISO_ROOT)/boot/limine
	mkdir -p $(RESUME_ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(RESUME_ISO_ROOT)/boot/jani.elf
	cp $(LIMINE_RESUME_CONFIG) $(RESUME_ISO_ROOT)/boot/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(RESUME_ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(RESUME_ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(RESUME_ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(RESUME_ISO_ROOT)/EFI/BOOT/
	test ! -e $(RESUME_ISO_ROOT)/boot/counter.wasm
	$(XORRISO) -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part \
		--efi-boot-image \
		--protective-msdos-label \
		$(RESUME_ISO_ROOT) \
		-o $(RESUME_ISO_IMAGE)
	$(LIMINE_DIR)/limine bios-install $(RESUME_ISO_IMAGE)

test: $(TEST_PMM_BIN) $(TEST_HEAP_BIN) $(TEST_OBJECT_TABLE_BIN) $(TEST_CAPABILITY_BIN) $(TEST_DERIVATION_BIN) $(TEST_OBJECT_HEADER_BIN) $(TEST_WAL_BIN) $(TEST_OBJECT_STORE_BIN) $(TEST_WRITE_ORDERING_BIN) $(TEST_WASM_SHIM_BIN) $(TEST_WASM_MODULE_BIN) $(TEST_SYSCALL_ARGS_BIN) $(TEST_COMPONENT_STATE_BIN) $(TEST_COMPONENT_SET_BIN) $(TEST_COMPONENT_MAILBOX_BIN) $(TEST_COMPONENT_UNINSTALL_BIN) $(TEST_IDLC_BIN) $(TEST_DETERMINISM_BIN)
	$(TEST_PMM_BIN)
	$(TEST_HEAP_BIN)
	$(TEST_OBJECT_TABLE_BIN)
	$(TEST_CAPABILITY_BIN)
	$(TEST_DERIVATION_BIN)
	$(TEST_OBJECT_HEADER_BIN)
	$(TEST_WAL_BIN)
	$(TEST_OBJECT_STORE_BIN)
	$(TEST_WRITE_ORDERING_BIN)
	$(TEST_WASM_SHIM_BIN)
	$(TEST_WASM_MODULE_BIN)
	$(TEST_SYSCALL_ARGS_BIN)
	$(TEST_COMPONENT_STATE_BIN)
	$(TEST_COMPONENT_SET_BIN)
	$(TEST_COMPONENT_MAILBOX_BIN)
	$(TEST_COMPONENT_UNINSTALL_BIN)
	$(TEST_IDLC_BIN)
	$(TEST_DETERMINISM_BIN)

$(TEST_PMM_BIN): $(PMM_SOURCE) $(HOSTED_DIR)/test_pmm.c $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/fake_memory_map.h $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(PMM_SOURCE) $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/test_pmm.c -o $(TEST_PMM_BIN)

$(TEST_HEAP_BIN): $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/hosted_heap.h $(HOSTED_DIR)/test_heap.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/test_heap.c -o $(TEST_HEAP_BIN)

$(TEST_OBJECT_TABLE_BIN): $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(HOSTED_DIR)/test_object_table.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(HOSTED_DIR)/test_object_table.c -o $(TEST_OBJECT_TABLE_BIN)

$(TEST_CAPABILITY_BIN): $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_capability.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_capability.c -o $(TEST_CAPABILITY_BIN)

$(TEST_DERIVATION_BIN): $(DERIVATION_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_derivation.c $(HOSTED_DIR)/check.h kernel/cap/derivation.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(DERIVATION_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_derivation.c -o $(TEST_DERIVATION_BIN)

$(OBJECT_HEADER_VALIDATE_HOSTED_OBJ): $(OBJECT_HEADER_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O Debug $(OBJECT_HEADER_VALIDATE_SOURCE) -femit-bin=$(OBJECT_HEADER_VALIDATE_HOSTED_OBJ)

$(TEST_OBJECT_HEADER_BIN): $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_object_header.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HOSTED_DIR)/test_object_header.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) -o $(TEST_OBJECT_HEADER_BIN)

$(WAL_VALIDATE_HOSTED_OBJ): $(WAL_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O Debug $(WAL_VALIDATE_SOURCE) -femit-bin=$(WAL_VALIDATE_HOSTED_OBJ)

$(TEST_WAL_BIN): $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_wal.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HOSTED_DIR)/test_wal.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $(TEST_WAL_BIN)

$(TEST_OBJECT_STORE_BIN): $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_object_store.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(HOSTED_DIR)/test_object_store.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $(TEST_OBJECT_STORE_BIN)

write-ordering-negative: $(TEST_WRITE_ORDERING_BIN)
	$(TEST_WRITE_ORDERING_BIN) negative

$(TEST_WRITE_ORDERING_BIN): $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_write_ordering.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(HOSTED_DIR)/test_write_ordering.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $(TEST_WRITE_ORDERING_BIN)

$(FUZZ_HEAP_BIN): $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/hosted_heap.h $(HOSTED_DIR)/fuzz_heap.c Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/fuzz_heap.c -o $(FUZZ_HEAP_BIN)

$(TEST_WASM_SHIM_BIN): $(WASM_SHIM_SOURCES) $(HOSTED_DIR)/test_wasm_shim.c $(HOSTED_DIR)/check.h kernel/wasm/shim/jani_libc.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DJANI_HOSTED $(WASM_SHIM_SOURCES) $(HOSTED_DIR)/test_wasm_shim.c -o $(TEST_WASM_SHIM_BIN)

$(FUZZ_WASM_SHIM_BIN): $(WASM_SHIM_SOURCES) $(HOSTED_DIR)/fuzz_wasm_shim.c kernel/wasm/shim/jani_libc.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) -DJANI_HOSTED $(WASM_SHIM_SOURCES) $(HOSTED_DIR)/fuzz_wasm_shim.c -o $(FUZZ_WASM_SHIM_BIN)

$(TEST_COMPONENT_STATE_BIN): $(INSTANCE_STATE_SOURCE) $(STRING_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_component_state.c $(HOSTED_DIR)/check.h kernel/wasm/component.h kernel/wasm/instance_state.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(INSTANCE_STATE_SOURCE) $(STRING_SOURCE) $(HOSTED_DIR)/test_component_state.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(INSTANCE_STATE_HOST_VALIDATOR) -o $(TEST_COMPONENT_STATE_BIN)

$(TEST_COMPONENT_SET_BIN): $(COMPONENT_SET_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_component_set.c $(HOSTED_DIR)/check.h kernel/wasm/component_set.h kernel/wasm/component.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(COMPONENT_SET_SOURCE) $(OBJECT_ID_SOURCE) $(HOSTED_DIR)/test_component_set.c -o $(TEST_COMPONENT_SET_BIN)

$(TEST_COMPONENT_MAILBOX_BIN): $(COMPONENT_SOURCE) $(INSTANCE_STATE_SOURCE) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(STRING_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_component_mailbox.c $(HOSTED_DIR)/check.h kernel/wasm/component.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(COMPONENT_SOURCE) $(INSTANCE_STATE_SOURCE) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(STRING_SOURCE) $(HOSTED_DIR)/test_component_mailbox.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(INSTANCE_STATE_HOST_VALIDATOR) -o $(TEST_COMPONENT_MAILBOX_BIN)

$(TEST_COMPONENT_UNINSTALL_BIN): $(COMPONENT_SOURCE) $(INSTANCE_STATE_SOURCE) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(STRING_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_component_uninstall.c $(HOSTED_DIR)/check.h kernel/wasm/component.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(COMPONENT_SOURCE) $(INSTANCE_STATE_SOURCE) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(STRING_SOURCE) $(HOSTED_DIR)/test_component_uninstall.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(INSTANCE_STATE_HOST_VALIDATOR) -o $(TEST_COMPONENT_UNINSTALL_BIN)

$(SYSCALL_ARGS_HOSTED_OBJ): $(SYSCALL_ARGS_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O Debug $(SYSCALL_ARGS_SOURCE) -femit-bin=$(SYSCALL_ARGS_HOSTED_OBJ)

$(TEST_DETERMINISM_BIN): $(HOSTED_DIR)/test_determinism.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HOSTED_DIR)/test_determinism.c -o $(TEST_DETERMINISM_BIN)

$(TEST_SYSCALL_ARGS_BIN): $(SYSCALL_ARGS_HOSTED_OBJ) $(HOSTED_DIR)/test_syscall_args.c $(HOSTED_DIR)/check.h kernel/wasm/syscall_args.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HOSTED_DIR)/test_syscall_args.c $(SYSCALL_ARGS_HOSTED_OBJ) -o $(TEST_SYSCALL_ARGS_BIN)

$(TEST_IDLC_BIN): $(IDLC_SOURCES) $(IDLC_HEADERS) $(HOSTED_DIR)/test_idlc.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(IDLC_SOURCES) $(HOSTED_DIR)/test_idlc.c -o $(TEST_IDLC_BIN)

$(IDLC_BIN): $(IDLC_SOURCES) $(IDLC_HEADERS) $(IDLC_DIR)/main.c Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(IDLC_SOURCES) $(IDLC_DIR)/main.c -o $(IDLC_BIN)

idlc: $(IDLC_BIN)

idl-generate: $(IDLC_BIN) $(IDL_SYSCALLS) $(IDL_RECORDS)
	mkdir -p $(GENERATED_DIR) sdk/zig sdk/c
	$(IDLC_BIN) --emit=zig     --out=sdk/zig/jani.zig                     $(IDL_SYSCALLS)
	$(IDLC_BIN) --emit=c       --out=sdk/c/jani.h                         $(IDL_SYSCALLS)
	$(IDLC_BIN) --emit=table   --out=$(GENERATED_DIR)/syscall_table.h     $(IDL_SYSCALLS)
	$(IDLC_BIN) --emit=conform --out=$(GENERATED_DIR)/records_conform.h   $(IDL_RECORDS)
	@echo "idl-generate: four files written"

idl-check: $(IDLC_BIN) $(IDL_SYSCALLS) $(IDL_RECORDS)
	@rm -rf $(BUILD_DIR)/idl-check && mkdir -p $(BUILD_DIR)/idl-check
	@$(IDLC_BIN) --emit=zig     --out=$(BUILD_DIR)/idl-check/jani.zig           $(IDL_SYSCALLS)
	@$(IDLC_BIN) --emit=c       --out=$(BUILD_DIR)/idl-check/jani.h             $(IDL_SYSCALLS)
	@$(IDLC_BIN) --emit=table   --out=$(BUILD_DIR)/idl-check/syscall_table.h    $(IDL_SYSCALLS)
	@$(IDLC_BIN) --emit=conform --out=$(BUILD_DIR)/idl-check/records_conform.h  $(IDL_RECORDS)
	@diff -u sdk/zig/jani.zig $(BUILD_DIR)/idl-check/jani.zig
	@diff -u sdk/c/jani.h $(BUILD_DIR)/idl-check/jani.h
	@diff -u $(GENERATED_DIR)/syscall_table.h $(BUILD_DIR)/idl-check/syscall_table.h
	@diff -u $(GENERATED_DIR)/records_conform.h $(BUILD_DIR)/idl-check/records_conform.h
	@echo "idl-check: generated files match their sources"

idl-negative: $(IDLC_BIN) $(IDL_SYSCALLS) $(IDL_RECORDS)
	tools/idl_negative.sh $(IDLC_BIN) $(BUILD_DIR) $(GENERATED_DIR)

$(FUZZ_SYSCALL_ARGS_BIN): $(SYSCALL_ARGS_HOSTED_OBJ) $(HOSTED_DIR)/fuzz_syscall_args.c kernel/wasm/syscall_args.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) $(HOSTED_DIR)/fuzz_syscall_args.c $(SYSCALL_ARGS_HOSTED_OBJ) -o $(FUZZ_SYSCALL_ARGS_BIN)

$(MODULE_VALIDATE_HOSTED_OBJ): $(MODULE_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O Debug $(MODULE_VALIDATE_SOURCE) -femit-bin=$(MODULE_VALIDATE_HOSTED_OBJ)

$(TEST_WASM_MODULE_BIN): $(MODULE_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_wasm_module.c $(HOSTED_DIR)/check.h kernel/wasm/module.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(MODULE_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/test_wasm_module.c -o $(TEST_WASM_MODULE_BIN)

$(FUZZ_WASM_MODULE_BIN): $(MODULE_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/fuzz_wasm_module.c kernel/wasm/module.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) $(MODULE_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/fuzz_wasm_module.c -o $(FUZZ_WASM_MODULE_BIN)

# Seeded with a valid module: random bytes essentially never produce the
# \0asm magic, so without a seed the fuzzer only ever exercises rejection
# and never reaches the section parser that actually walks untrusted lengths.
#
# HONEST SCOPE: the validator is a Zig object and is NOT instrumented for
# libFuzzer -- coverage stays flat at 8 whether or not the seed is present,
# because libFuzzer only sees the C harness. Zig's -ffuzz drives Zig's own
# fuzzer, not this one. So this campaign is random testing, not coverage-
# guided fuzzing. What still protects us is Zig's Debug-mode runtime safety:
# an out-of-bounds index or integer overflow inside the validator panics and
# aborts the run. The same limitation applies to fuzz-object-store, which
# links the header and WAL validators the same way.
fuzz-syscall-args: $(FUZZ_SYSCALL_ARGS_BIN)
	mkdir -p $(BUILD_DIR)/fuzz-corpus-syscall-args
	$(FUZZ_SYSCALL_ARGS_BIN) $(BUILD_DIR)/fuzz-corpus-syscall-args -runs=$(FUZZ_RUNS) -max_len=64 -timeout=5

fuzz-wasm-module: $(FUZZ_WASM_MODULE_BIN)
	mkdir -p $(BUILD_DIR)/fuzz-corpus-wasm-module
	printf '\000asm\001\000\000\000\001\002\252\273\003\000' \
	  > $(BUILD_DIR)/fuzz-corpus-wasm-module/seed-valid
	$(FUZZ_WASM_MODULE_BIN) $(BUILD_DIR)/fuzz-corpus-wasm-module -runs=$(FUZZ_RUNS) -max_len=1024 -timeout=5

# -fno-entry: this is a library of exports, not a program with a main.
# --export=run is what makes the handler visible to the host.
$(HELLO_WASM): $(HELLO_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall \
	  -fno-entry -rdynamic --export=run $(HELLO_SOURCE) -femit-bin=$(HELLO_WASM)

$(COUNTER_WASM): $(COUNTER_SOURCE) $(SDK_ZIG) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall \
	  -fno-entry -rdynamic --stack 16384 --export=jani_init \
	  --export=jani_on_timer --export=jani_on_message \
	  --dep jani -Mroot=$(COUNTER_SOURCE) -Mjani=$(SDK_ZIG) \
	  -femit-bin=$(COUNTER_WASM)

counter-wasm: $(COUNTER_WASM)

hello-wasm: $(HELLO_WASM)

$(MODULE_VALIDATE_OBJ): $(MODULE_VALIDATE_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O Debug $(MODULE_VALIDATE_SOURCE) -femit-bin=$(MODULE_VALIDATE_OBJ)

$(SYSCALL_ARGS_OBJ): $(SYSCALL_ARGS_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O Debug $(SYSCALL_ARGS_SOURCE) -femit-bin=$(SYSCALL_ARGS_OBJ)

$(WASM_RUNTIME_OBJ): $(WASM_RUNTIME_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) $(WAMR_INCLUDES) $(WAMR_DEFINES) -c $(WASM_RUNTIME_SOURCE) -o $(WASM_RUNTIME_OBJ)

$(SYSCALLS_OBJ): $(SYSCALLS_SOURCE) kernel/wasm/syscalls.h kernel/wasm/component.h kernel/wasm/component_set.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) $(WAMR_INCLUDES) $(WAMR_DEFINES) -c $(SYSCALLS_SOURCE) -o $(SYSCALLS_OBJ)

$(COMPONENT_OBJ): $(COMPONENT_SOURCE) kernel/wasm/component.h kernel/wasm/instance_state.h kernel/cap/cap_table.h $(GENERATED_DIR)/records_conform.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(COMPONENT_SOURCE) -o $(COMPONENT_OBJ)

$(COMPONENT_SET_OBJ): $(COMPONENT_SET_SOURCE) kernel/wasm/component_set.h kernel/wasm/component.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(COMPONENT_SET_SOURCE) -o $(COMPONENT_SET_OBJ)

$(INSTANCE_STATE_OBJ): $(INSTANCE_STATE_SOURCE) kernel/wasm/component.h kernel/wasm/instance_state.h kernel/cap/cap_table.h Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(INSTANCE_STATE_SOURCE) -o $(INSTANCE_STATE_OBJ)

fuzz-wasm-shim: $(FUZZ_WASM_SHIM_BIN)
	mkdir -p $(BUILD_DIR)/fuzz-corpus-wasm-shim
	$(FUZZ_WASM_SHIM_BIN) $(BUILD_DIR)/fuzz-corpus-wasm-shim -runs=$(FUZZ_RUNS) -max_len=256 -timeout=5

fuzz-heap: $(FUZZ_HEAP_BIN)
	mkdir -p $(BUILD_DIR)/fuzz-corpus
	$(FUZZ_HEAP_BIN) $(BUILD_DIR)/fuzz-corpus -runs=$(FUZZ_RUNS) -max_len=4096 -timeout=5

$(FUZZ_OBJECT_STORE_BIN): $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(HOSTED_DIR)/fuzz_object_store.c Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(HOSTED_DIR)/fuzz_object_store.c $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $(FUZZ_OBJECT_STORE_BIN)

fuzz-object-store: $(FUZZ_OBJECT_STORE_BIN)
	mkdir -p $(BUILD_DIR)/fuzz-corpus-store
	$(FUZZ_OBJECT_STORE_BIN) $(BUILD_DIR)/fuzz-corpus-store -runs=$(FUZZ_RUNS) -max_len=4096 -timeout=5

run: $(ISO_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -serial stdio

run-debug: $(ISO_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -serial stdio -s -S

crash-test: $(ISO_IMAGE)
	tools/crash_test_qemu.sh $(CRASH_CYCLES)

wow-demo: $(ISO_IMAGE)
	tools/wow_demo_qemu.sh $(WOW_CYCLES) $(WOW_SECONDS)

zero-install-test: $(ISO_IMAGE) $(RESUME_ISO_IMAGE)
	tools/zero_install_qemu.sh $(WOW_SECONDS)

# Proves the gate above can fail. Each seed breaks resume in a different way;
# the gate must reject every one of them. A gate nobody has seen fail is not
# yet a gate -- the same reasoning as model-check-negative.
wow-demo-negative:
	@set -e; for bug in 1 2 3; do \
	  echo "== seeded resume bug $$bug: expecting the gate to fail =="; \
	  rm -f $(MAIN_OBJ) $(KERNEL_ELF) $(ISO_IMAGE); \
	  $(MAKE) --no-print-directory EXTRA_CFLAGS=-DJANI_WOW_BUG=$$bug $(ISO_IMAGE) >/dev/null; \
	  if tools/wow_demo_qemu.sh 2 $(WOW_SECONDS) >/dev/null 2>&1; then \
	    echo "MISSED: seeded bug $$bug went undetected"; \
	    rm -f $(MAIN_OBJ) $(KERNEL_ELF) $(ISO_IMAGE); \
	    exit 1; \
	  fi; \
	  echo "   caught"; \
	done; \
	rm -f $(MAIN_OBJ) $(KERNEL_ELF) $(ISO_IMAGE); \
	echo; \
	echo "PASS: the wow-demo gate rejected all 3 seeded resume bugs"

clean:
	rm -rf $(BUILD_DIR)

# --- TLA+ model checking (Phase 2, blueprint R9) -----------------------------
TLA_TOOLS := third_party/tla2tools.jar
TLA_TOOLS_URL := https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar
TLA_TOOLS_SHA256 := 936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88
TLC_FLAGS := -workers auto -deadlock -cleanup
BUG_CFGS := WalCommitBugNoFlush WalCommitBugTruncateFirst \
	WalCommitBugSkipChecksum

# The pinned sha256 is trust-on-first-use: hashed from the official
# github.com/tlaplus release download on 2026-07-15 (upstream publishes no
# checksums). The committed jar is the artifact of record; this rule only
# re-fetches on clean checkouts, and the pin detects a changed download.
# Download to a temp path and verify BEFORE moving into place: a failed
# check must not leave a jar where make would treat it as a valid target.
$(TLA_TOOLS):
	mkdir -p third_party
	curl -fL -o $(TLA_TOOLS).tmp $(TLA_TOOLS_URL)
	echo "$(TLA_TOOLS_SHA256)  $(TLA_TOOLS).tmp" | sha256sum -c -
	mv $(TLA_TOOLS).tmp $(TLA_TOOLS)

model-check: $(TLA_TOOLS)
	cd docs/models && java -XX:+UseParallelGC -cp $(abspath $(TLA_TOOLS)) \
	  tlc2.TLC $(TLC_FLAGS) -config WalCommit.cfg WalCommit.tla

model-check-negative: $(TLA_TOOLS)
	@set -e; for cfg in $(BUG_CFGS); do \
	  echo "== $$cfg: expecting TLC to find a violation =="; \
	  out=$$(cd docs/models && java -XX:+UseParallelGC \
	    -cp $(abspath $(TLA_TOOLS)) tlc2.TLC $(TLC_FLAGS) \
	    -config $$cfg.cfg WalCommit.tla 2>&1 || true); \
	  if printf '%s\n' "$$out" | \
	       grep -Eq "is violated|Temporal properties were violated"; then \
	    echo "   ok: violation found"; \
	  else \
	    printf '%s\n' "$$out" | tail -20; \
	    echo "MODEL TOO WEAK: $$cfg produced no violation"; exit 1; \
	  fi; \
	done
