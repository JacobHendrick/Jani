NODE_IDENTITY_C := kernel/net/node_identity.c
NODE_IDENTITY_H := kernel/net/node_identity.h
NODE_IDENTITY_ZIG := kernel/net/node_identity_decode.zig
NODE_IDENTITY_OBJ := $(BUILD_DIR)/net/node_identity.o
NODE_IDENTITY_DECODE_OBJ := $(BUILD_DIR)/net/node_identity_decode.o
NODE_IDENTITY_HOST_OBJ := $(BUILD_DIR)/net/host/node_identity_decode.o
NODE_IDENTITY_TEST := $(HOSTED_DIR)/test_node_identity.c
KERNEL_OBJECTS += $(NODE_IDENTITY_OBJ) $(NODE_IDENTITY_DECODE_OBJ)

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
