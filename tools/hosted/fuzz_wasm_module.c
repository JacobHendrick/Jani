#include <stdint.h>
#include <stddef.h>

#include "../../kernel/wasm/module.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint32_t sections;
    int accepted;

    sections = 0xFFFFFFFFu;
    accepted = wasm_module_validate(data, size, &sections);

    if ((accepted != 0) && (accepted != 1)) {
        __builtin_trap();
    }

    if (accepted && (sections == 0xFFFFFFFFu)) {
        __builtin_trap();
    }

    if (!accepted && (size >= 8)) {
        uint32_t ignored = 0;

        if (wasm_module_validate(data, size, &ignored) != 0) {
            __builtin_trap();
        }
    }

    (void)wasm_module_validate(data, size, 0);

    return 0;
}
