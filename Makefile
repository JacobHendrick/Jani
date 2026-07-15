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
SERIAL_OBJ := $(BUILD_DIR)/serial.o
PRINTK_OBJ := $(BUILD_DIR)/printk.o
STRING_OBJ := $(BUILD_DIR)/string.o
KERNEL_OBJECTS := $(MAIN_OBJ) $(GDT_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(INTERRUPTS_OBJ) $(PIC_OBJ) $(PIT_OBJ) $(KEYBOARD_OBJ) $(MEMORY_MAP_OBJ) $(LAYOUT_OBJ) $(PMM_OBJ) $(VMM_OBJ) $(SERIAL_OBJ) $(PRINTK_OBJ) $(STRING_OBJ)

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
	-Wall \
	-Wextra \
	-Werror \
	-O0 \
	-g \
	-fsanitize=undefined \
	-fsanitize-trap=undefined

LDFLAGS := -T $(LINKER_SCRIPT)

HOSTED_DIR := tools/hosted
TEST_PMM_BIN := $(BUILD_DIR)/test_pmm

.PHONY: all check-tools kernel iso run run-debug test clean

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

$(PMM_OBJ): $(PMM_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(PMM_SOURCE) -o $(PMM_OBJ)

$(VMM_OBJ): $(VMM_SOURCE) Makefile
	mkdir -p $(BUILD_DIR) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VMM_SOURCE) -o $(VMM_OBJ)

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

test: $(TEST_PMM_BIN)
	$(TEST_PMM_BIN)

$(TEST_PMM_BIN): $(PMM_SOURCE) $(HOSTED_DIR)/test_pmm.c $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/fake_memory_map.h $(HOSTED_DIR)/check.h Makefile
	mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(PMM_SOURCE) $(HOSTED_DIR)/stubs.c $(HOSTED_DIR)/fake_memory_map.c $(HOSTED_DIR)/test_pmm.c -o $(TEST_PMM_BIN)

run: $(ISO_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -serial stdio

run-debug: $(ISO_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMAGE) -serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR)

# --- TLA+ model checking (Phase 2, blueprint R9) -----------------------------
TLA_TOOLS := third_party/tla2tools.jar
TLA_TOOLS_URL := https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar
TLA_TOOLS_SHA256 := 936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88
TLC_FLAGS := -workers auto -deadlock -cleanup

$(TLA_TOOLS):
	mkdir -p third_party
	curl -fL -o $(TLA_TOOLS) $(TLA_TOOLS_URL)
	echo "$(TLA_TOOLS_SHA256)  $(TLA_TOOLS)" | sha256sum -c -
