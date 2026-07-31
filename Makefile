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
	-fsanitize=address,undefined -fno-omit-frame-pointer
FUZZ_CFLAGS := $(HOST_CFLAGS) -fsanitize=fuzzer
FUZZ_RUNS ?= 10000
CRASH_CYCLES ?= 25

BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
ZIG_GLOBAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-global-cache)
ZIG_LOCAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-local-cache)
ZIG_ENV := env ZIG_GLOBAL_CACHE_DIR=$(ZIG_GLOBAL_CACHE_DIR) ZIG_LOCAL_CACHE_DIR=$(ZIG_LOCAL_CACHE_DIR)

KERNEL_ELF := $(BUILD_DIR)/jani.elf
ISO_IMAGE := $(BUILD_DIR)/jani.iso
MAIN_OBJ := $(BUILD_DIR)/main.o
GDT_OBJ := $(BUILD_DIR)/gdt.o
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
KERNEL_OBJECTS := $(MAIN_OBJ) $(GDT_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(INTERRUPTS_OBJ) $(PIC_OBJ) $(PIT_OBJ) $(KEYBOARD_OBJ) $(MEMORY_MAP_OBJ) $(LAYOUT_OBJ) $(PMM_OBJ) $(VMM_OBJ) $(MMIO_OBJ) $(HEAP_OBJ) $(FREE_LIST_OBJ) $(HEAP_BACKEND_OBJ) $(OBJECT_ID_OBJ) $(OBJECT_TABLE_OBJ) $(OBJECT_HEADER_VALIDATE_OBJ) $(WAL_VALIDATE_OBJ) $(OBJECT_STORE_OBJ) $(PCI_OBJ) $(VIRTIO_PCI_OBJ) $(VIRTQUEUE_OBJ) $(VIRTIO_BLK_OBJ) $(SERIAL_OBJ) $(PRINTK_OBJ) $(STRING_OBJ)

KERNEL_SOURCE := kernel/boot/main.c
GDT_SOURCE := kernel/arch/gdt.c
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
LINKER_SCRIPT := kernel/boot/linker.ld
LIMINE_CONFIG := kernel/boot/limine.conf
LIMINE_DIR := third_party/limine

CFLAGS := -target x86_64-freestanding-none \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pie \
	-mcmodel=kernel \
	-mno-red-zone \
	-mno-sse \
	-mno-sse2 \
	-mno-mmx \
	-Wall \
	-Wextra \
	-Werror \
	-O0 \
	-g \
	-fsanitize=undefined \
	-fsanitize-trap=undefined

ZIG_KERNEL_TARGET := -target x86_64-freestanding-none -mcpu=x86_64-sse-sse2-mmx

LDFLAGS := -T $(LINKER_SCRIPT)

HOSTED_DIR := tools/hosted
TEST_PMM_BIN := $(BUILD_DIR)/test_pmm
TEST_HEAP_BIN := $(BUILD_DIR)/test_heap
TEST_OBJECT_TABLE_BIN := $(BUILD_DIR)/test_object_table
TEST_OBJECT_HEADER_BIN := $(BUILD_DIR)/test_object_header
TEST_WAL_BIN := $(BUILD_DIR)/test_wal
TEST_OBJECT_STORE_BIN := $(BUILD_DIR)/test_object_store
OBJECT_HEADER_VALIDATE_HOSTED_OBJ := $(BUILD_DIR)/object_header_validate_hosted.o
WAL_VALIDATE_HOSTED_OBJ := $(BUILD_DIR)/wal_validate_hosted.o
FUZZ_HEAP_BIN := $(BUILD_DIR)/fuzz_heap
FUZZ_OBJECT_STORE_BIN := $(BUILD_DIR)/fuzz_object_store

.PHONY: all check-tools kernel iso run run-debug test fuzz-heap \
	fuzz-object-store model-check model-check-negative crash-test clean

all: iso

check-tools:
	$(ZIG) version
	$(LD) --version
	$(XORRISO) -version
	$(QEMU) --version
	$(LIMINE_DIR)/limine --version

$(MAIN_OBJ): $(KERNEL_SOURCE) Makefile
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

$(KERNEL_ELF): $(KERNEL_OBJECTS) $(LINKER_SCRIPT)
	mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $(KERNEL_ELF)

kernel: $(KERNEL_ELF)

iso: $(ISO_IMAGE)

$(ISO_IMAGE): $(KERNEL_ELF) $(LIMINE_CONFIG)
	mkdir -p $(ISO_ROOT)/boot
	mkdir -p $(ISO_ROOT)/boot/limine
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/jani.elf
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

test: $(TEST_PMM_BIN) $(TEST_HEAP_BIN) $(TEST_OBJECT_TABLE_BIN) $(TEST_OBJECT_HEADER_BIN) $(TEST_WAL_BIN) $(TEST_OBJECT_STORE_BIN)
	$(TEST_PMM_BIN)
	$(TEST_HEAP_BIN)
	$(TEST_OBJECT_TABLE_BIN)
	$(TEST_OBJECT_HEADER_BIN)
	$(TEST_WAL_BIN)
	$(TEST_OBJECT_STORE_BIN)

$(TEST_PMM_BIN): $(PMM_SOURCE) $(HOSTED_DIR)/test_pmm.c $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/fake_memory_map.h $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(PMM_SOURCE) $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/test_pmm.c -o $(TEST_PMM_BIN)

$(TEST_HEAP_BIN): $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/hosted_heap.h $(HOSTED_DIR)/test_heap.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/test_heap.c -o $(TEST_HEAP_BIN)

$(TEST_OBJECT_TABLE_BIN): $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(HOSTED_DIR)/test_object_table.c $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(HOSTED_DIR)/test_object_table.c -o $(TEST_OBJECT_TABLE_BIN)

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

$(FUZZ_HEAP_BIN): $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/hosted_heap.h $(HOSTED_DIR)/fuzz_heap.c Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(FUZZ_CFLAGS) $(HEAP_SOURCE) $(FREE_LIST_SOURCE) $(HOSTED_DIR)/heap_backend_hosted.c $(HOSTED_DIR)/fuzz_heap.c -o $(FUZZ_HEAP_BIN)

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
