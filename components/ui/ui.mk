UI_CXX := clang++
UI_CXXFLAGS := -std=c++20 $(HOST_CFLAGS) -fno-exceptions -fno-rtti
UI_CANVAS_SOURCE := components/ui/canvas.cpp
UI_CANVAS_HEADER := components/ui/canvas.hpp
UI_TEST_SOURCE := tools/hosted/test_ui_canvas.cpp
UI_PREVIEW_SOURCE := tools/hosted/ui_preview.cpp
UI_TEST_BIN := $(BUILD_DIR)/test_ui_canvas
UI_PREVIEW_BIN := $(BUILD_DIR)/ui_preview
UI_PREVIEW_IMAGE := $(BUILD_DIR)/ui-preview.ppm
UI_WASM_OBJ := $(BUILD_DIR)/ui/canvas.wasm.o

$(UI_TEST_BIN): $(UI_CANVAS_SOURCE) $(UI_CANVAS_HEADER) $(UI_TEST_SOURCE) components/ui/ui.mk Makefile
	mkdir -p $(@D)
	$(UI_CXX) $(UI_CXXFLAGS) $(UI_CANVAS_SOURCE) $(UI_TEST_SOURCE) -o $@

$(UI_PREVIEW_BIN): $(UI_CANVAS_SOURCE) $(UI_CANVAS_HEADER) $(UI_PREVIEW_SOURCE) components/ui/ui.mk Makefile
	mkdir -p $(@D)
	$(UI_CXX) $(UI_CXXFLAGS) $(UI_CANVAS_SOURCE) $(UI_PREVIEW_SOURCE) -o $@

$(UI_PREVIEW_IMAGE): $(UI_PREVIEW_BIN)
	$(UI_PREVIEW_BIN) $@

$(UI_WASM_OBJ): $(UI_CANVAS_SOURCE) $(UI_CANVAS_HEADER) components/ui/ui.mk Makefile
	mkdir -p $(@D) $(ZIG_GLOBAL_CACHE_DIR) $(ZIG_LOCAL_CACHE_DIR)
	$(ZIG_ENV) $(ZIG) c++ -target wasm32-freestanding-none -std=c++20 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -Wall -Wextra -Werror -c $(UI_CANVAS_SOURCE) -o $@

.PHONY: test-ui-canvas ui-preview ui-wasm-check
test-ui-canvas: $(UI_TEST_BIN)
	$(UI_TEST_BIN)

ui-preview: $(UI_PREVIEW_IMAGE)

ui-wasm-check: $(UI_WASM_OBJ)

test: test-ui-canvas
