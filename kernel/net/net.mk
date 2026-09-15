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
NET_PARSER_SOURCES := tools/hosted/test_net_parsers.zig kernel/net/ethernet.zig kernel/net/ipv4.zig kernel/net/udp.zig kernel/net/frame_decode.zig

$(NET_PARSER_TEST): $(NET_PARSER_SOURCES) kernel/net/net.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec --dep ethernet --dep ipv4 --dep udp --dep frame_decode -Mroot=tools/hosted/test_net_parsers.zig -Methernet=kernel/net/ethernet.zig -Mipv4=kernel/net/ipv4.zig -Mudp=kernel/net/udp.zig --dep ethernet --dep ipv4 --dep udp -Mframe_decode=kernel/net/frame_decode.zig -femit-bin=$@

.PHONY: test-net-parsers net-parser-negative
test-net-parsers: $(NET_PARSER_TEST)
	$(NET_PARSER_TEST)

test: test-net-parsers

$(BUILD_DIR)/net/negative/ipv4.zig: kernel/net/ipv4.zig kernel/net/net.mk
	mkdir -p $(@D)
	sed 's/flags_and_offset \& 0x3fff/flags_and_offset \& 0x1fff/' $< > $@

$(BUILD_DIR)/test_net_parsers_negative: tools/hosted/test_net_parsers.zig kernel/net/ethernet.zig $(BUILD_DIR)/net/negative/ipv4.zig kernel/net/udp.zig kernel/net/frame_decode.zig kernel/net/net.mk Makefile
	mkdir -p $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) test -O ReleaseSafe --test-no-exec --dep ethernet --dep ipv4 --dep udp --dep frame_decode -Mroot=tools/hosted/test_net_parsers.zig -Methernet=kernel/net/ethernet.zig -Mipv4=$(BUILD_DIR)/net/negative/ipv4.zig -Mudp=kernel/net/udp.zig --dep ethernet --dep ipv4 --dep udp -Mframe_decode=kernel/net/frame_decode.zig -femit-bin=$@

net-parser-negative: $(BUILD_DIR)/test_net_parsers_negative
	@if $(BUILD_DIR)/test_net_parsers_negative >$(BUILD_DIR)/net-parser-negative.log 2>&1; then \
	    echo "FAIL: accepted seeded fragment-mask defect"; exit 1; \
	fi
	@grep -F 'ipv4 rejects first fragment' $(BUILD_DIR)/net-parser-negative.log
	@echo "PASS: rejected seeded fragment-mask defect"

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
