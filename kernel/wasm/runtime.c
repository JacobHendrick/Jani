#include "runtime.h"

#include "wasm_export.h"

#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/heap.h"

#define JANI_WASM_STACK_SIZE  (64 * 1024)
#define JANI_WASM_HEAP_SIZE   (16 * 1024)
#define JANI_WASM_ERROR_SIZE 128

static void *jani_wasm_walloc(unsigned int size) {
    return kmalloc((size_t)size);
}

static void *jani_wasm_wrealloc(void *ptr, unsigned int size) {
    return krealloc(ptr, (size_t)size); 
}

static void jani_wasm_wfree(void *ptr) {
    kfree(ptr);
}

static void jani_log_wrapper(
    wasm_exec_env_t exec_env,
    uint32_t offset,
    uint32_t length
) {
    wasm_module_inst_t instance;
    const char *text;
    uint32_t index;

    instance = wasm_runtime_get_module_inst(exec_env);

    if (!wasm_runtime_validate_app_addr(instance, (uint64_t)offset,
                                           (uint64_t)length)) {
        wasm_runtime_set_exception(instance, "jani_log: address out of bounds");
        return;
    }

    text = (const char *)wasm_runtime_addr_app_to_native(instance, (uint64_t)offset);

    for (index = 0; index < length; index++) {
        printk("%c", text[index]);
    }
}

static NativeSymbol jani_native_symbols[] = {
    { "jani_log", (void*)jani_log_wrapper, "(ii)", NULL },
};

int jani_wasm_run_module(const uint8_t *bytes, size_t length) {
    RuntimeInitArgs init_args;
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t run_function;
    wasm_memory_inst_t memory;
    char error_buffer[JANI_WASM_ERROR_SIZE];
    uint64_t heap_before;
    uint64_t heap_after;
    int32_t export_count;
    int result;

    module = NULL;
    instance = NULL;
    exec_env = NULL;
    result = 0;
    error_buffer[0] = '\0';
    heap_before = kheap_used_bytes();

    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func = (void *)jani_wasm_walloc;
    init_args.mem_alloc_option.allocator.realloc_func = (void *)jani_wasm_wrealloc;
    init_args.mem_alloc_option.allocator.free_func = (void *)jani_wasm_wfree;
    init_args.native_module_name = "env";
    init_args.native_symbols = jani_native_symbols;
    init_args.n_native_symbols =
        (uint32_t)(sizeof(jani_native_symbols) / sizeof(NativeSymbol));

    if (!wasm_runtime_full_init(&init_args)) {
        kputs("ERROR: wamr: runtime init failed\n");
        return 0;
    }
    kputs("wamr: runtime init success\n");

    module = wasm_runtime_load((uint8_t *)bytes, (uint32_t)length, error_buffer, sizeof(error_buffer));
    if (module == NULL) {
        printk("ERROR: wamr: load failed: %s\n", error_buffer);
        goto teardown;
    }

    export_count = wasm_runtime_get_export_count(module);
    printk("wamr: module loaded, %d exports\n", (int)export_count);

    instance = wasm_runtime_instantiate(module, JANI_WASM_STACK_SIZE, JANI_WASM_HEAP_SIZE, error_buffer, sizeof(error_buffer));

    if (instance == NULL) {
        printk("ERROR: wamr: instantiate failed: %s\n", error_buffer);
        goto teardown;
    }

    memory = wasm_runtime_get_default_memory(instance);
    if (memory == NULL) {
        printk("ERROR: wamr: get default memory failed\n");
        goto teardown;
    }
    printk("wamr: instance created, %d memory pages\n",
           (int)wasm_memory_get_cur_page_count(memory));

    run_function = wasm_runtime_lookup_function(instance, "run");
    if (run_function == NULL) {
        kputs("ERROR: wamr: module exports no 'run' function\n");
        goto teardown;
    }

    exec_env = wasm_runtime_create_exec_env(instance, JANI_WASM_STACK_SIZE);
    if (exec_env == NULL) {
        kputs("ERROR: wamr: create exec env failed\n");
        goto teardown;
    }

    if (!wasm_runtime_call_wasm(exec_env, run_function, 0, NULL)) {
        printk("ERROR: wamr: call 'run' failed: %s\n",
               wasm_runtime_get_exception(instance));
        goto teardown;
    }

    result = 1;

teardown:
    if (exec_env != NULL) {
        wasm_runtime_destroy_exec_env(exec_env);
    }
    if (instance != NULL) {
        wasm_runtime_deinstantiate(instance);
    }
    if (module != NULL) {
        wasm_runtime_unload(module);
    }
    wasm_runtime_destroy();

    heap_after = kheap_used_bytes();
    printk("wamr: instance destroyed, heap high-water %d -> %d bytes\n",
           (int)heap_before, (int)heap_after);

    return result;
}