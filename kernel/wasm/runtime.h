#ifndef JANI_KERNEL_WASM_RUNTIME_H
#define JANI_KERNEL_WASM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

int jani_wasm_run_module(const uint8_t *bytes, size_t length);

#endif