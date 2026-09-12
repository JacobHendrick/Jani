#ifndef JANI_KERNEL_WASM_RUNTIME_H
#define JANI_KERNEL_WASM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

struct component;

int jani_wasm_run_module(const uint8_t *bytes, size_t length);

int jani_wasm_runtime_start(void);

void jani_wasm_runtime_stop(void);

int jani_wasm_instance_create(
    const uint8_t *bytes,
    size_t length,
    void **module_out,
    void **instance_out,
    void **exec_env_out,
    void **owned_bytes_out
);

void jani_wasm_instance_destroy(
    void *module,
    void *instance,
    void *exec_env,
    void *owned_bytes
);

int jani_wasm_instance_memory(
    void *instance,
    uint8_t **base_out,
    size_t *size_out
);

int jani_wasm_instance_memory_grow(void *instance, size_t required_bytes);

int jani_wasm_instance_call(void *instance, void *exec_env, const char *name);
int jani_wasm_instance_has_handler(void *instance, const char *name);

void jani_wasm_set_current_component(struct component *component);

#endif
