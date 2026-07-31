#ifndef JANI_KERNEL_WASM_MODULE_H
#define JANI_KERNEL_WASM_MODULE_H

#include <stddef.h>
#include <stdint.h>

int wasm_module_validate(
    const uint8_t *bytes,
    size_t byte_count,
    uint32_t *section_count_out
);

#endif
