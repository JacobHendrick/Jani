# Fault-seeded builds are isolated from the normal kernel objects.
.PHONY: phase5-negative session-replay-negative replica-rights-negative migration-fence-negative net-component-negative fuzz-distribution
session-replay-negative: $(BUILD_DIR)/net/host/wire_validate.o
	mkdir -p $(BUILD_DIR)/phase5-negative
	sed 's/view.sequence <= s->receive_sequence/0/' kernel/net/session.c > $(BUILD_DIR)/phase5-negative/session.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/net -Ithird_party/monocypher/src $(BUILD_DIR)/phase5-negative/session.c $(NET_CRYPTO_C) tools/hosted/test_net_session.c $(BUILD_DIR)/net/host/wire_validate.o -o $(BUILD_DIR)/phase5-negative/session
	@set -eu; status=0; $(BUILD_DIR)/phase5-negative/session > $(BUILD_DIR)/phase5-negative/session.log 2>&1 || status=$$?; test $$status -eq 1; grep -F '!net_session_open(&sb, packet, length' $(BUILD_DIR)/phase5-negative/session.log

replica-rights-negative: $(BUILD_DIR)/test_replica
	mkdir -p $(BUILD_DIR)/phase5-negative
	sed 's/(!scope->owned \&\& !(scope->rights \& REPLICA_WRITE))/0/' kernel/net/replica.c > $(BUILD_DIR)/phase5-negative/replica.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/net -Ithird_party/monocypher/src $(filter-out kernel/net/replica.c,$(REPLICA_HOST_C)) $(BUILD_DIR)/phase5-negative/replica.c tools/hosted/test_replica.c $(REPLICA_HOST_ZIG) -o $(BUILD_DIR)/phase5-negative/replica
	@set -eu; status=0; $(BUILD_DIR)/phase5-negative/replica > $(BUILD_DIR)/phase5-negative/replica.log 2>&1 || status=$$?; test $$status -eq 1; grep -F 'replica_set(&b.replica, id, (const uint8_t *)"forbidden", 9) == 0' $(BUILD_DIR)/phase5-negative/replica.log

migration-fence-negative: $(BUILD_DIR)/test_migration
	mkdir -p $(BUILD_DIR)/phase5-negative
	sed 's/ || component_is_fenced(store, root_id)//' $(COMPONENT_SOURCE) > $(BUILD_DIR)/phase5-negative/component.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/wasm -Ithird_party/monocypher/src $(filter-out $(COMPONENT_SOURCE),$(MIGRATION_HOST_C)) $(BUILD_DIR)/phase5-negative/component.c tools/hosted/test_migration.c $(MIGRATION_HOST_ZIG) -o $(BUILD_DIR)/phase5-negative/migration
	@set -eu; status=0; $(BUILD_DIR)/phase5-negative/migration > $(BUILD_DIR)/phase5-negative/migration.log 2>&1 || status=$$?; test $$status -eq 1; grep -F '!component_resume(&a.store,a.component.root_id,&blocked)' $(BUILD_DIR)/phase5-negative/migration.log

net-component-negative: $(BUILD_DIR)/test_net_component
	mkdir -p $(BUILD_DIR)/phase5-negative
	sed 's/caller != \&driver/((void)caller, 0)/g' kernel/drivers/net_component.c > $(BUILD_DIR)/phase5-negative/net_component.c
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/drivers $(BUILD_DIR)/phase5-negative/net_component.c $(CAP_TABLE_SOURCE) $(CAPABILITY_SOURCE) $(OBJECT_ID_SOURCE) tools/hosted/test_net_component.c $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) -o $(BUILD_DIR)/phase5-negative/net_component
	@set -eu; status=0; $(BUILD_DIR)/phase5-negative/net_component > $(BUILD_DIR)/phase5-negative/net_component.log 2>&1 || status=$$?; test $$status -eq 1; grep -F 'net_component_request(&outsider,bytes,sizeof(bytes)) == JANI_EPERM' $(BUILD_DIR)/phase5-negative/net_component.log

phase5-negative: session-replay-negative replica-rights-negative migration-fence-negative net-component-negative phase5-model-negative migration-model-negative

DISTRIBUTION_ZIG = $(BUILD_DIR)/net/host/crdt.o $(BUILD_DIR)/net/host/wire_validate.o $(BUILD_DIR)/net/host/replica_validate.o $(BUILD_DIR)/net/host/migration_validate.o $(BUILD_DIR)/net/host/config_validate.o $(INSTANCE_STATE_HOST_VALIDATOR) $(MODULE_VALIDATE_HOSTED_OBJ)
$(BUILD_DIR)/fuzz_distribution: tools/hosted/fuzz_distribution.c $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $$(DISTRIBUTION_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ)
	$(HOST_CC) $(FUZZ_CFLAGS) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $< $(DISTRIBUTION_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $@

$(BUILD_DIR)/distribution_seeds: tools/hosted/fuzz_distribution.c $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $$(DISTRIBUTION_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ)
	$(HOST_CC) $(HOST_CFLAGS) -DJANI_FUZZ_SEEDS $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE) $< $(DISTRIBUTION_ZIG) $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) -o $@

fuzz-distribution: $(BUILD_DIR)/fuzz_distribution $(BUILD_DIR)/distribution_seeds
	mkdir -p $(BUILD_DIR)/distribution-corpus
	$(BUILD_DIR)/distribution_seeds $(BUILD_DIR)/distribution-corpus
	$(BUILD_DIR)/fuzz_distribution -runs=20000 -max_len=1048576 $(BUILD_DIR)/distribution-corpus

.PHONY: phase5-entropy-negative
phase5-entropy-negative: iso $(BUILD_DIR)/phase5-jani.elf $(BUILD_DIR)/net_provision
	PHASE5_CPU=max,-apic,-rdrand PHASE5_EXPECT_ENTROPY_FAILURE=1 PHASE5_LOG_PREFIX=$(abspath $(BUILD_DIR)/phase5-entropy) sh tools/phase5_network_qemu.sh
phase5-negative: phase5-entropy-negative
