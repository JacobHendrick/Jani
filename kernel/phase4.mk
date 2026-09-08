PHASE4_C := kernel/cap/lineage.c kernel/cap/domain.c kernel/cap/provenance.c kernel/sched/scheduler.c kernel/replay/record.c kernel/wasm/service.c
PHASE4_ZIG := kernel/cap/lineage_validate.zig kernel/cap/provenance_validate.zig kernel/wasm/mailbox_validate.zig kernel/sched/trace_validate.zig kernel/replay/record_validate.zig kernel/wasm/service_validate.zig
PHASE4_OBJECTS := $(patsubst kernel/%.c,$(BUILD_DIR)/phase4/%.o,$(PHASE4_C))
PHASE4_ZIG_OBJECTS := $(patsubst kernel/%.zig,$(BUILD_DIR)/phase4/%.o,$(PHASE4_ZIG))
PHASE4_ZIG_OBJECTS += $(BUILD_DIR)/phase4/drivers/block_validate.o
PHASE4_HOST_ZIG := $(patsubst kernel/%.zig,$(BUILD_DIR)/phase4/host/%.o,$(PHASE4_ZIG))
PHASE4_HOST_ZIG += $(BUILD_DIR)/phase4/host/drivers/block_validate.o
INSTANCE_STATE_HOST_VALIDATOR := $(BUILD_DIR)/phase4/host/wasm/mailbox_validate.o
$(TEST_COMPONENT_STATE_BIN) $(TEST_COMPONENT_MAILBOX_BIN) $(TEST_COMPONENT_UNINSTALL_BIN): $(INSTANCE_STATE_HOST_VALIDATOR)
PHASE4_HEADERS := $(wildcard kernel/cap/*.h kernel/wasm/*.h kernel/sched/*.h kernel/replay/*.h)
KERNEL_OBJECTS += $(PHASE4_OBJECTS) $(PHASE4_ZIG_OBJECTS)
KERNEL_OBJECTS += $(BUILD_DIR)/phase4/drivers/block_component.o $(BUILD_DIR)/block_driver_blob.o $(BUILD_DIR)/block_supervisor_blob.o
KERNEL_OBJECTS += $(BUILD_DIR)/phase4/wasm/phase4_demo.o $(BUILD_DIR)/phase4_v1_blob.o $(BUILD_DIR)/phase4_v2_blob.o
CFLAGS += -MMD -MP

$(KERNEL_OBJECTS): $(PHASE4_HEADERS)

$(BUILD_DIR)/phase4/%.o: kernel/%.c $(PHASE4_HEADERS) kernel/phase4.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/phase4/%.o: kernel/%.zig kernel/phase4.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/phase4/host/%.o: kernel/%.zig kernel/phase4.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O Debug $< -femit-bin=$@

PHASE4_HOST_C := $(PHASE4_C) $(COMPONENT_SOURCE) $(COMPONENT_SET_SOURCE) $(INSTANCE_STATE_SOURCE) \
	$(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(DERIVATION_SOURCE) $(OBJECT_ID_SOURCE) \
	$(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $(STRING_SOURCE)

$(BUILD_DIR)/test_phase4: tools/hosted/test_phase4.c $(PHASE4_HOST_C) $(PHASE4_HEADERS) $(PHASE4_HOST_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) kernel/phase4.mk
	$(HOST_CC) $(HOST_CFLAGS) -DJANI_HOSTED $(PHASE4_HOST_C) $< $(PHASE4_HOST_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $@

.PHONY: test-phase4
test-phase4: $(BUILD_DIR)/test_phase4
	$(BUILD_DIR)/test_phase4

test: test-phase4

$(BUILD_DIR)/test_phase4_negative_%: tools/hosted/test_phase4.c $(PHASE4_HOST_C) $(PHASE4_HEADERS) $(PHASE4_HOST_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) kernel/phase4.mk
	$(HOST_CC) $(HOST_CFLAGS) -DJANI_HOSTED -DJANI_PHASE4_BUG_$* $(PHASE4_HOST_C) $< $(PHASE4_HOST_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $@

.PHONY: phase4-negative
phase4-negative: $(addprefix $(BUILD_DIR)/test_phase4_negative_,GRANT REVOKE REPLAY SWAP)
	@set -eu; for bug in GRANT REVOKE REPLAY SWAP; do \
	  if $(BUILD_DIR)/test_phase4_negative_$$bug >$(BUILD_DIR)/phase4-negative-$$bug.log 2>&1; then \
	    echo "FAIL: Phase 4 gate accepted seeded $$bug defect"; exit 1; \
	  fi; \
	  grep 'CHECK FAILED' $(BUILD_DIR)/phase4-negative-$$bug.log; \
	  echo "PASS: rejected seeded $$bug defect"; \
	done

$(BUILD_DIR)/block_driver.wasm: components/block_driver/driver.zig $(SDK_ZIG)
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall -fno-entry -rdynamic --dep jani -Mroot=$< -Mjani=$(SDK_ZIG) -femit-bin=$@

$(BUILD_DIR)/block_supervisor.wasm: components/block_driver/supervisor.zig $(SDK_ZIG)
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall -fno-entry -rdynamic --dep jani -Mroot=$< -Mjani=$(SDK_ZIG) -femit-bin=$@

$(BUILD_DIR)/block_driver_blob.o: $(BUILD_DIR)/block_driver.wasm
	$(LD) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@

$(BUILD_DIR)/block_supervisor_blob.o: $(BUILD_DIR)/block_supervisor.wasm
	$(LD) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@

$(BUILD_DIR)/phase4_v%.wasm: components/phase4/v%.zig components/phase4/service.zig $(SDK_ZIG)
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -O ReleaseSmall -fno-entry -rdynamic --stack 32768 --global-base=65536 --dep jani -Mroot=$< -Mjani=$(SDK_ZIG) -femit-bin=$@

$(BUILD_DIR)/phase4_v%_blob.o: $(BUILD_DIR)/phase4_v%.wasm
	$(LD) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@

-include $(KERNEL_OBJECTS:.o=.d)

.SECONDARY: $(BUILD_DIR)/phase4_v1.wasm $(BUILD_DIR)/phase4_v2.wasm
.PHONY: phase4-demo
phase4-demo:
	@mkdir -p $(BUILD_DIR)
	@set -eu; \
	  restore() { rm -f $(MAIN_OBJ) $(KERNEL_ELF) $(ISO_IMAGE); $(MAKE) --no-print-directory iso >$(BUILD_DIR)/phase4-restore.log 2>&1; }; \
	  trap restore EXIT; \
	  rm -f $(MAIN_OBJ) $(KERNEL_ELF) $(ISO_IMAGE); \
	  $(MAKE) --no-print-directory EXTRA_CFLAGS=-DJANI_PHASE4_DEMO iso >$(BUILD_DIR)/phase4-build.log 2>&1 || { tail -30 $(BUILD_DIR)/phase4-build.log; exit 1; }; \
	  sh tools/phase4_demo_qemu.sh
