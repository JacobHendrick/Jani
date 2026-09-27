NODE_IDENTITY_C := kernel/net/node_identity.c
NODE_IDENTITY_H := kernel/net/node_identity.h
NODE_IDENTITY_ZIG := kernel/net/node_identity_decode.zig
NODE_IDENTITY_OBJ := $(BUILD_DIR)/net/node_identity.o
NODE_IDENTITY_DECODE_OBJ := $(BUILD_DIR)/net/node_identity_decode.o
NODE_IDENTITY_HOST_OBJ := $(BUILD_DIR)/net/host/node_identity_decode.o
NODE_IDENTITY_TEST := $(HOSTED_DIR)/test_node_identity.c
KERNEL_OBJECTS += $(NODE_IDENTITY_OBJ) $(NODE_IDENTITY_DECODE_OBJ)

FRAME_DECODE_ZIG := kernel/net/frame_decode.zig
FRAME_DECODE_H := kernel/net/frame_decode.h
FRAME_DECODE_OBJ := $(BUILD_DIR)/net/frame_decode.o
FRAME_DECODE_HOST_OBJ := $(BUILD_DIR)/net/host/frame_decode.o
FRAME_DECODE_TEST := $(HOSTED_DIR)/test_frame_decode.c
FRAME_DECODE_MODULES := kernel/net/ethernet.zig kernel/net/ipv4.zig kernel/net/udp.zig
KERNEL_OBJECTS += $(FRAME_DECODE_OBJ)

FRAME_ENCODE_ZIG := kernel/net/frame_encode.zig
FRAME_ENCODE_OBJ := $(BUILD_DIR)/net/frame_encode.o
KERNEL_OBJECTS += $(FRAME_ENCODE_OBJ)

$(FRAME_ENCODE_OBJ): $(FRAME_ENCODE_ZIG) $(FRAME_DECODE_MODULES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone --dep ethernet --dep ipv4 --dep udp -Mroot=$(FRAME_ENCODE_ZIG) -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig -femit-bin=$@

$(FRAME_DECODE_OBJ): $(FRAME_DECODE_ZIG) $(FRAME_DECODE_MODULES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone --dep ethernet --dep ipv4 --dep udp -Mroot=$(FRAME_DECODE_ZIG) -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig -femit-bin=$@

$(FRAME_DECODE_HOST_OBJ): $(FRAME_DECODE_ZIG) $(FRAME_DECODE_MODULES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe --dep ethernet --dep ipv4 --dep udp -Mroot=$(FRAME_DECODE_ZIG) -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig -femit-bin=$@

$(BUILD_DIR)/test_frame_decode: $(FRAME_DECODE_TEST) $(FRAME_DECODE_H) $(FRAME_DECODE_HOST_OBJ) $(HOSTED_DIR)/check.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(FRAME_DECODE_TEST) $(FRAME_DECODE_HOST_OBJ) -o $@

.PHONY: test-frame-decode
test-frame-decode: $(BUILD_DIR)/test_frame_decode
	$(BUILD_DIR)/test_frame_decode

test: test-frame-decode

VIRTIO_NET_SOURCE := kernel/drivers/virtio_net.c
VIRTIO_NET_HEADER := kernel/drivers/virtio_net.h
VIRTIO_NET_OBJ := $(BUILD_DIR)/virtio_net.o
VIRTIO_NET_TEST := $(HOSTED_DIR)/test_virtio_net.c
KERNEL_OBJECTS += $(VIRTIO_NET_OBJ)

$(VIRTIO_NET_OBJ): $(VIRTIO_NET_SOURCE) $(VIRTIO_NET_HEADER) kernel/drivers/pci.h kernel/drivers/virtio_pci.h kernel/drivers/virtqueue.h kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $(VIRTIO_NET_SOURCE) -o $@

$(BUILD_DIR)/test_virtio_net: $(VIRTIO_NET_TEST) $(VIRTIO_NET_SOURCE) $(VIRTIO_NET_HEADER) kernel/drivers/pci.h kernel/drivers/virtio_pci.h kernel/drivers/virtqueue.h $(HOSTED_DIR)/check.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(VIRTIO_NET_SOURCE) $(VIRTIO_NET_TEST) -o $@

.PHONY: test-virtio-net
test-virtio-net: $(BUILD_DIR)/test_virtio_net
	$(BUILD_DIR)/test_virtio_net

test: test-virtio-net

VIRTQUEUE_TEST := $(HOSTED_DIR)/test_virtqueue.c

$(BUILD_DIR)/test_virtqueue: $(VIRTQUEUE_TEST) kernel/drivers/virtqueue.c kernel/drivers/virtqueue.h kernel/drivers/virtio_pci.h $(HOSTED_DIR)/check.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) kernel/drivers/virtqueue.c $(VIRTQUEUE_TEST) -o $@

.PHONY: test-virtqueue
test-virtqueue: $(BUILD_DIR)/test_virtqueue
	$(BUILD_DIR)/test_virtqueue

test: test-virtqueue

$(NODE_IDENTITY_OBJ): $(NODE_IDENTITY_C) $(NODE_IDENTITY_H) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(NODE_IDENTITY_DECODE_OBJ): $(NODE_IDENTITY_ZIG) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(NODE_IDENTITY_HOST_OBJ): $(NODE_IDENTITY_ZIG) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

$(BUILD_DIR)/test_node_identity_zig: $(NODE_IDENTITY_ZIG) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec $< -femit-bin=$@

$(BUILD_DIR)/test_node_identity: $(NODE_IDENTITY_TEST) $(NODE_IDENTITY_C) $(NODE_IDENTITY_H) $(NODE_IDENTITY_HOST_OBJ) $(HOSTED_DIR)/check.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(NODE_IDENTITY_C) $(NODE_IDENTITY_TEST) $(NODE_IDENTITY_HOST_OBJ) -o $@

.PHONY: test-node-identity node-identity-negative
test-node-identity: $(BUILD_DIR)/test_node_identity_zig $(BUILD_DIR)/test_node_identity
	$(BUILD_DIR)/test_node_identity_zig
	$(BUILD_DIR)/test_node_identity

test: test-node-identity

NET_PARSER_TEST := $(BUILD_DIR)/test_net_parsers
NET_PARSER_SOURCES := tools/hosted/test_net_parsers.zig kernel/net/ethernet.zig kernel/net/ipv4.zig kernel/net/udp.zig kernel/net/frame_decode.zig $(FRAME_ENCODE_ZIG)

$(NET_PARSER_TEST): $(NET_PARSER_SOURCES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec --dep ethernet --dep ipv4 --dep udp --dep frame_decode --dep frame_encode -Mroot=tools/hosted/test_net_parsers.zig -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig --dep ethernet --dep ipv4 --dep udp -Mframe_decode=kernel/net/frame_decode.zig --dep ethernet --dep ipv4 --dep udp -Mframe_encode=$(FRAME_ENCODE_ZIG) -femit-bin=$@

.PHONY: test-net-parsers net-parser-negative
test-net-parsers: $(NET_PARSER_TEST)
	$(NET_PARSER_TEST)

test: test-net-parsers

$(BUILD_DIR)/net/negative/ipv4.zig: kernel/net/ipv4.zig kernel/net/net.mk
	mkdir -p $(@D)
	sed 's/flags_and_offset \& 0x3fff/flags_and_offset \& 0x1fff/' $< > $@

$(BUILD_DIR)/test_net_parsers_negative: $(NET_PARSER_SOURCES) $(BUILD_DIR)/net/negative/ipv4.zig kernel/net/net.mk Makefile
	mkdir -p $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec --dep ethernet --dep ipv4 --dep udp --dep frame_decode --dep frame_encode -Mroot=tools/hosted/test_net_parsers.zig -Methernet=kernel/net/ethernet.zig -Mipv4=$(BUILD_DIR)/net/negative/ipv4.zig -Mudp=kernel/net/udp.zig --dep ethernet --dep ipv4 --dep udp -Mframe_decode=kernel/net/frame_decode.zig --dep ethernet --dep ipv4 --dep udp -Mframe_encode=$(FRAME_ENCODE_ZIG) -femit-bin=$@

net-parser-negative: $(BUILD_DIR)/test_net_parsers_negative
	@if $(BUILD_DIR)/test_net_parsers_negative >$(BUILD_DIR)/net-parser-negative.log 2>&1; then \
	    echo "FAIL: accepted seeded fragment-mask defect"; exit 1; \
	fi
	@grep -F 'ipv4 rejects first fragment' $(BUILD_DIR)/net-parser-negative.log
	@echo "PASS: rejected seeded fragment-mask defect"

$(BUILD_DIR)/net/negative/frame_encode.zig: $(FRAME_ENCODE_ZIG) kernel/net/net.mk
	mkdir -p $(@D)
	sed 's/if (overlaps(output, frame_length, payload))/if (false)/' $< > $@

$(BUILD_DIR)/test_frame_encode_negative: $(NET_PARSER_SOURCES) $(BUILD_DIR)/net/negative/frame_encode.zig kernel/net/net.mk Makefile
	mkdir -p $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec --dep ethernet --dep ipv4 --dep udp --dep frame_decode --dep frame_encode -Mroot=tools/hosted/test_net_parsers.zig -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig --dep ethernet --dep ipv4 --dep udp -Mframe_decode=kernel/net/frame_decode.zig --dep ethernet --dep ipv4 --dep udp -Mframe_encode=$(BUILD_DIR)/net/negative/frame_encode.zig -femit-bin=$@

.PHONY: frame-encode-negative
frame-encode-negative: $(BUILD_DIR)/test_frame_encode_negative
	@if $(BUILD_DIR)/test_frame_encode_negative >$(BUILD_DIR)/frame-encode-negative.log 2>&1; then \
	    echo "FAIL: accepted seeded overlap defect"; exit 1; \
	fi
	@grep -F 'frame encoder rejects invalid inputs without writing' $(BUILD_DIR)/frame-encode-negative.log
	@echo "PASS: rejected seeded overlap defect"

$(BUILD_DIR)/net/negative/node_identity_decode.zig: $(NODE_IDENTITY_ZIG) kernel/net/net.mk
	mkdir -p $(@D)
	sed 's/if (length != public_key_size)/if (length < public_key_size)/' $< > $@

$(BUILD_DIR)/net/negative/node_identity_decode.o: $(BUILD_DIR)/net/negative/node_identity_decode.zig kernel/net/net.mk Makefile
	mkdir -p $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

$(BUILD_DIR)/test_node_identity_negative: $(NODE_IDENTITY_TEST) $(NODE_IDENTITY_C) $(NODE_IDENTITY_H) $(BUILD_DIR)/net/negative/node_identity_decode.o $(HOSTED_DIR)/check.h kernel/net/net.mk Makefile
	$(HOST_CC) $(HOST_CFLAGS) $(NODE_IDENTITY_C) $(NODE_IDENTITY_TEST) $(BUILD_DIR)/net/negative/node_identity_decode.o -o $@

node-identity-negative: $(BUILD_DIR)/test_node_identity_negative
	@if $(BUILD_DIR)/test_node_identity_negative >$(BUILD_DIR)/node-identity-negative.log 2>&1; then \
	    echo "FAIL: accepted seeded oversized-key defect"; exit 1; \
	fi
	@grep -F 'node_public_key_decode(input, invalid_lengths[i], &output.key) == 0' $(BUILD_DIR)/node-identity-negative.log
	@echo "PASS: rejected seeded oversized-key defect"

FRAME_ENCODE_HOST_OBJ := $(BUILD_DIR)/net/host/frame_encode.o
NET_CRYPTO_C := third_party/monocypher/src/monocypher.c third_party/monocypher/src/optional/monocypher-ed25519.c
NET_CRYPTO_H := third_party/monocypher/src/monocypher.h third_party/monocypher/src/optional/monocypher-ed25519.h
NET_CRYPTO_OBJ := $(BUILD_DIR)/net/monocypher.o $(BUILD_DIR)/net/monocypher-ed25519.o
NET_SESSION_OBJ := $(BUILD_DIR)/net/session.o $(BUILD_DIR)/net/wire_validate.o
NET_HEADERS := kernel/net/frame_encode.h kernel/net/transmit.h kernel/net/session.h $(NET_CRYPTO_H)
KERNEL_OBJECTS += $(BUILD_DIR)/net/transmit.o $(NET_SESSION_OBJ) $(NET_CRYPTO_OBJ)

$(FRAME_ENCODE_HOST_OBJ): $(FRAME_ENCODE_ZIG) $(FRAME_DECODE_MODULES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe --dep ethernet --dep ipv4 --dep udp -Mroot=$(FRAME_ENCODE_ZIG) -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig -femit-bin=$@

$(BUILD_DIR)/net/transmit.o: kernel/net/transmit.c $(NET_HEADERS) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/session.o: kernel/net/session.c $(NET_HEADERS) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) -Ithird_party/monocypher/src $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/monocypher.o: third_party/monocypher/src/monocypher.c $(NET_CRYPTO_H) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/monocypher-ed25519.o: third_party/monocypher/src/optional/monocypher-ed25519.c $(NET_CRYPTO_H) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) -Ithird_party/monocypher/src $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/wire_validate.o: kernel/net/wire_validate.zig kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/net/host/wire_validate.o: kernel/net/wire_validate.zig kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

$(BUILD_DIR)/test_frame_encode: tools/hosted/test_frame_encode.c kernel/net/transmit.c $(NET_HEADERS) $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) kernel/net/net.mk Makefile
	$(HOST_CC) $(HOST_CFLAGS) kernel/net/transmit.c $< $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) -o $@

$(BUILD_DIR)/test_net_session: tools/hosted/test_net_session.c kernel/net/session.c $(NET_HEADERS) $(NET_CRYPTO_C) $(BUILD_DIR)/net/host/wire_validate.o kernel/net/net.mk Makefile
	$(HOST_CC) -Ithird_party/monocypher/src $(HOST_CFLAGS) kernel/net/session.c $(NET_CRYPTO_C) $< $(BUILD_DIR)/net/host/wire_validate.o -o $@

.PHONY: test-frame-encode test-net-session verify-monocypher
test-frame-encode: $(BUILD_DIR)/test_frame_encode
	$(BUILD_DIR)/test_frame_encode
test-net-session: $(BUILD_DIR)/test_net_session
	$(BUILD_DIR)/test_net_session
verify-monocypher:
	cd third_party/monocypher && sha256sum -c SHA256SUMS

test: test-frame-encode test-net-session verify-monocypher

KERNEL_OBJECTS += $(BUILD_DIR)/net/crdt.o

$(BUILD_DIR)/net/crdt.o: kernel/net/crdt.zig kernel/net/crdt.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/net/host/crdt.o: kernel/net/crdt.zig kernel/net/crdt.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

$(BUILD_DIR)/test_crdt: tools/hosted/test_crdt.c kernel/net/crdt.h $(BUILD_DIR)/net/host/crdt.o kernel/net/net.mk Makefile
	$(HOST_CC) $(HOST_CFLAGS) $< $(BUILD_DIR)/net/host/crdt.o -o $@

.PHONY: test-crdt phase5-model-check phase5-model-negative
test-crdt: $(BUILD_DIR)/test_crdt
	$(BUILD_DIR)/test_crdt

test: test-crdt

phase5-model-check: third_party/tla2tools.jar
	mkdir -p $(BUILD_DIR)/phase5-model
	cd docs/models && java -XX:+UseParallelGC -cp $(abspath third_party/tla2tools.jar) tlc2.TLC -workers 2 -metadir $(abspath $(BUILD_DIR)/phase5-model) -config Distribution.cfg Distribution

phase5-model-negative: third_party/tla2tools.jar
	@mkdir -p $(BUILD_DIR)/phase5-model-negative
	@set -eu; status=0; cd docs/models; \
	  java -XX:+UseParallelGC -cp $(abspath third_party/tla2tools.jar) tlc2.TLC -workers 2 -metadir $(abspath $(BUILD_DIR)/phase5-model-negative) -config DistributionBroken.cfg Distribution >$(abspath $(BUILD_DIR)/phase5-model-negative.log) 2>&1 || status=$$?; \
	  test $$status -eq 12; \
	  grep -F 'Invariant NoRollback is violated.' $(abspath $(BUILD_DIR)/phase5-model-negative.log); \
	  echo "PASS: rejected stale-overwrite convergence defect"

KERNEL_OBJECTS += $(BUILD_DIR)/net/replica.o $(BUILD_DIR)/net/replica_validate.o

$(BUILD_DIR)/net/replica.o: kernel/net/replica.c kernel/net/replica.h kernel/net/crdt.h $(NET_HEADERS) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) -Ithird_party/monocypher/src $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/replica_validate.o: kernel/net/replica_validate.zig kernel/net/replica.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/net/host/replica_validate.o: kernel/net/replica_validate.zig kernel/net/replica.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

REPLICA_HOST_C := kernel/net/replica.c kernel/net/session.c $(NET_CRYPTO_C) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE)
REPLICA_HOST_ZIG := $(BUILD_DIR)/net/host/crdt.o $(BUILD_DIR)/net/host/replica_validate.o $(BUILD_DIR)/net/host/wire_validate.o $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ)

$(BUILD_DIR)/test_replica: tools/hosted/test_replica.c $(REPLICA_HOST_C) kernel/net/replica.h kernel/net/crdt.h $(NET_HEADERS) $(REPLICA_HOST_ZIG) kernel/net/net.mk Makefile
	$(HOST_CC) -Ithird_party/monocypher/src $(HOST_CFLAGS) $(REPLICA_HOST_C) $< $(REPLICA_HOST_ZIG) -o $@

.PHONY: test-replica
test-replica: $(BUILD_DIR)/test_replica
	$(BUILD_DIR)/test_replica

test: test-replica

KERNEL_OBJECTS += $(BUILD_DIR)/net/keys.o

$(BUILD_DIR)/net/keys.o: kernel/net/keys.c kernel/net/keys.h $(NET_HEADERS) kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

REPLICA_HOST_C += kernel/net/keys.c

$(BUILD_DIR)/test_replica: kernel/net/keys.c kernel/net/keys.h

KERNEL_OBJECTS += $(BUILD_DIR)/net/peer.o $(BUILD_DIR)/net/entropy.o $(BUILD_DIR)/net/config_validate.o

$(BUILD_DIR)/net/peer.o: kernel/net/peer.c kernel/net/peer.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/entropy.o: kernel/net/entropy.c kernel/net/entropy.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/config_validate.o: kernel/net/config_validate.zig kernel/net/config.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/net/host/config_validate.o: kernel/net/config_validate.zig kernel/net/config.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

$(BUILD_DIR)/net_provision: tools/net_provision.c kernel/net/session.c $(NET_CRYPTO_C) $(NET_HEADERS) $(BUILD_DIR)/net/host/wire_validate.o kernel/net/net.mk Makefile
	$(HOST_CC) -Ithird_party/monocypher/src $(HOST_CFLAGS) tools/net_provision.c kernel/net/session.c $(NET_CRYPTO_C) $(BUILD_DIR)/net/host/wire_validate.o -o $@

REPLICA_HOST_C += kernel/net/peer.c kernel/net/transmit.c
REPLICA_HOST_ZIG += $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) $(BUILD_DIR)/net/host/config_validate.o
$(BUILD_DIR)/test_replica: kernel/net/peer.c kernel/net/transmit.c kernel/net/peer.h $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) $(BUILD_DIR)/net/host/config_validate.o

KERNEL_OBJECTS += $(BUILD_DIR)/net/demo.o
$(BUILD_DIR)/net/demo.o: kernel/net/demo.c kernel/net/demo.h kernel/net/peer.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/phase5-main.o: $(KERNEL_SOURCE) kernel/net/demo.h kernel/net/net.mk Makefile
	$(ZIG_ENV) $(CC) $(CFLAGS) -DJANI_PHASE5_DEMO -c $< -o $@

.SECONDEXPANSION:
$(BUILD_DIR)/phase5-jani.elf: $$(filter-out $$(MAIN_OBJ),$$(KERNEL_OBJECTS)) $(BUILD_DIR)/phase5-main.o $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) $(filter-out $(MAIN_OBJ),$(KERNEL_OBJECTS)) $(BUILD_DIR)/phase5-main.o -o $@

.PHONY: phase5-network-test phase5-demo phase5-test
phase5-network-test phase5-demo: phase5-test
phase5-test: iso $(BUILD_DIR)/phase5-jani.elf $(BUILD_DIR)/net_provision
	sh tools/phase5_network_qemu.sh

KERNEL_OBJECTS += $(BUILD_DIR)/net/migration.o $(BUILD_DIR)/net/migration_validate.o

$(BUILD_DIR)/net/migration.o: kernel/net/migration.c kernel/net/migration.h kernel/wasm/component.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net/migration_validate.o: kernel/net/migration_validate.zig kernel/net/migration.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj $(ZIG_KERNEL_TARGET) -O ReleaseSafe -mcmodel=kernel -mno-red-zone $< -femit-bin=$@

$(BUILD_DIR)/net/host/migration_validate.o: kernel/net/migration_validate.zig kernel/net/migration.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-obj -O ReleaseSafe $< -femit-bin=$@

.PHONY: migration-model-check migration-model-negative
migration-model-check: third_party/tla2tools.jar
	mkdir -p $(BUILD_DIR)/migration-model
	cd docs/models && java -XX:+UseParallelGC -cp $(abspath third_party/tla2tools.jar) tlc2.TLC -workers 2 -metadir $(abspath $(BUILD_DIR)/migration-model) -config Migration.cfg Migration

migration-model-negative: third_party/tla2tools.jar
	@mkdir -p $(BUILD_DIR)/migration-model-negative
	@set -eu; status=0; cd docs/models; \
	  java -XX:+UseParallelGC -cp $(abspath third_party/tla2tools.jar) tlc2.TLC -workers 2 -metadir $(abspath $(BUILD_DIR)/migration-model-negative) -config MigrationBroken.cfg Migration >$(abspath $(BUILD_DIR)/migration-model-negative.log) 2>&1 || status=$$?; \
	  test $$status -eq 12; \
	  grep -F 'Invariant NoDualOwner is violated.' $(abspath $(BUILD_DIR)/migration-model-negative.log); \
	  echo "PASS: rejected activation without durable source fence"

MIGRATION_HOST_C := kernel/net/migration.c kernel/net/session.c $(NET_CRYPTO_C) $(COMPONENT_SOURCE) $(COMPONENT_SET_SOURCE) $(INSTANCE_STATE_SOURCE) $(CAPABILITY_SOURCE) $(CAP_TABLE_SOURCE) $(OBJECT_ID_SOURCE) $(OBJECT_TABLE_SOURCE) $(OBJECT_STORE_SOURCE)
MIGRATION_HOST_ZIG = $(BUILD_DIR)/net/host/migration_validate.o $(BUILD_DIR)/net/host/wire_validate.o $(OBJECT_HEADER_VALIDATE_HOSTED_OBJ) $(WAL_VALIDATE_HOSTED_OBJ) $(INSTANCE_STATE_HOST_VALIDATOR) $(MODULE_VALIDATE_HOSTED_OBJ)

$(BUILD_DIR)/test_migration: tools/hosted/test_migration.c $(MIGRATION_HOST_C) $$(MIGRATION_HOST_ZIG) kernel/net/migration.h kernel/wasm/component.h kernel/net/net.mk Makefile
	$(HOST_CC) -Ithird_party/monocypher/src $(HOST_CFLAGS) $(MIGRATION_HOST_C) $< $(MIGRATION_HOST_ZIG) -o $@

.PHONY: test-migration
test-migration: $(BUILD_DIR)/test_migration
	$(BUILD_DIR)/test_migration
test: test-migration

KERNEL_OBJECTS += $(BUILD_DIR)/net/net_component.o $(BUILD_DIR)/net_driver_blob.o

$(BUILD_DIR)/net_driver.wasm: components/net_driver/driver.zig $(SDK_ZIG) $(FRAME_DECODE_ZIG) $(FRAME_DECODE_MODULES) kernel/net/net.mk
	mkdir -p $(@D)
	$(ZIG_ENV) $(ZIG) build-exe -target wasm32-freestanding -mcpu=mvp -O ReleaseSafe -fno-entry -rdynamic --stack 16384 --dep jani --dep frame_decode -Mroot=$< -Mjani=$(SDK_ZIG) --dep ethernet --dep ipv4 --dep udp -Mframe_decode=$(FRAME_DECODE_ZIG) -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig -femit-bin=$@

$(BUILD_DIR)/net_driver_blob.o: $(BUILD_DIR)/net_driver.wasm
	$(LD) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@

$(BUILD_DIR)/net/net_component.o: kernel/drivers/net_component.c kernel/drivers/net_component.h kernel/net/net.mk Makefile
	mkdir -p $(@D)
	$(ZIG_ENV) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_net_component: tools/hosted/test_net_component.c kernel/drivers/net_component.c kernel/drivers/net_component.h $(CAP_TABLE_SOURCE) $(CAPABILITY_SOURCE) $(OBJECT_ID_SOURCE) $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) kernel/net/net.mk
	$(HOST_CC) $(HOST_CFLAGS) kernel/drivers/net_component.c $(CAP_TABLE_SOURCE) $(CAPABILITY_SOURCE) $(OBJECT_ID_SOURCE) $< $(FRAME_ENCODE_HOST_OBJ) $(FRAME_DECODE_HOST_OBJ) -o $@
.PHONY: test-net-component
test-net-component: $(BUILD_DIR)/test_net_component
	$(BUILD_DIR)/test_net_component
test: test-net-component

include kernel/net/phase5.mk
