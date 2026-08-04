#include "runtime.h"

#include "wasm_export.h"

#include "component.h"
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

static int32_t jani_log_wrapper(
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
        return -1;
    }

    text = (const char *)wasm_runtime_addr_app_to_native(instance, (uint64_t)offset);

    for (index = 0; index < length; index++) {
        printk("%c", text[index]);
    }

    return (int32_t)length;
}

static struct component *jani_current_component;

void jani_wasm_set_current_component(struct component *component) {
    jani_current_component = component;
}

static int32_t jani_timer_set_wrapper(
    wasm_exec_env_t exec_env,
    int64_t delay_ticks
) {
    (void)exec_env;

    if ((jani_current_component == NULL) || (delay_ticks < 0)) {
        return -1;
    }

    jani_current_component->timer_deadline =
        jani_current_component->logical_time + (uint64_t)delay_ticks;
    jani_current_component->timer_armed = 1;
    return 0;
}

static NativeSymbol jani_native_symbols[] = {
    { "jani_log", (void*)jani_log_wrapper, "(ii)i", NULL },
    { "jani_timer_set", (void*)jani_timer_set_wrapper, "(I)i", NULL },
};

static int jani_runtime_started;

int jani_wasm_runtime_start(void) {
    RuntimeInitArgs init_args;

    if (jani_runtime_started) {
        return 1;
    }

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

    jani_runtime_started = 1;
    return 1;
}

void jani_wasm_runtime_stop(void) {
    if (!jani_runtime_started) {
        return;
    }

    wasm_runtime_destroy();
    jani_runtime_started = 0;
}

int jani_wasm_instance_create(
    const uint8_t *bytes,
    size_t length,
    void **module_out,
    void **instance_out,
    void **exec_env_out
) {
    char error_buffer[JANI_WASM_ERROR_SIZE];
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;

    if ((bytes == NULL) || (module_out == NULL) ||
        (instance_out == NULL) || (exec_env_out == NULL)) {
        return 0;
    }

    *module_out = NULL;
    *instance_out = NULL;
    *exec_env_out = NULL;
    error_buffer[0] = '\0';

    module = wasm_runtime_load((uint8_t *)bytes, (uint32_t)length,
                               error_buffer, sizeof(error_buffer));
    if (module == NULL) {
        printk("ERROR: wamr: load failed: %s\n", error_buffer);
        return 0;
    }

    instance = wasm_runtime_instantiate(module, JANI_WASM_STACK_SIZE,
                                        JANI_WASM_HEAP_SIZE, error_buffer,
                                        sizeof(error_buffer));
    if (instance == NULL) {
        printk("ERROR: wamr: instantiate failed: %s\n", error_buffer);
        wasm_runtime_unload(module);
        return 0;
    }

    exec_env = wasm_runtime_create_exec_env(instance, JANI_WASM_STACK_SIZE);
    if (exec_env == NULL) {
        kputs("ERROR: wamr: create exec env failed\n");
        wasm_runtime_deinstantiate(instance);
        wasm_runtime_unload(module);
        return 0;
    }

    *module_out = module;
    *instance_out = instance;
    *exec_env_out = exec_env;
    return 1;
}

void jani_wasm_instance_destroy(void *module, void *instance, void *exec_env) {
    if (exec_env != NULL) {
        wasm_runtime_destroy_exec_env((wasm_exec_env_t)exec_env);
    }
    if (instance != NULL) {
        wasm_runtime_deinstantiate((wasm_module_inst_t)instance);
    }
    if (module != NULL) {
        wasm_runtime_unload((wasm_module_t)module);
    }
}

int jani_wasm_instance_memory(
    void *instance,
    uint8_t **base_out,
    size_t *size_out
) {
    wasm_memory_inst_t memory;

    if ((instance == NULL) || (base_out == NULL) || (size_out == NULL)) {
        return 0;
    }

    memory = wasm_runtime_get_memory((wasm_module_inst_t)instance, 0);
    if (memory == NULL) {
        return 0;
    }

    *base_out = (uint8_t *)wasm_memory_get_base_address(memory);
    *size_out = (size_t)(wasm_memory_get_cur_page_count(memory) *
                         wasm_memory_get_bytes_per_page(memory));

    return (*base_out != NULL) ? 1 : 0;
}

int jani_wasm_instance_memory_grow(void *instance, size_t required_bytes) {
    wasm_memory_inst_t memory;
    uint64_t bytes_per_page;
    uint64_t current_bytes;
    uint64_t missing;
    uint64_t pages;

    if (instance == NULL) {
        return 0;
    }

    memory = wasm_runtime_get_memory((wasm_module_inst_t)instance, 0);
    if (memory == NULL) {
        return 0;
    }

    bytes_per_page = wasm_memory_get_bytes_per_page(memory);
    if (bytes_per_page == 0) {
        return 0;
    }

    current_bytes = wasm_memory_get_cur_page_count(memory) * bytes_per_page;
    if ((uint64_t)required_bytes <= current_bytes) {
        return 1;
    }

    missing = (uint64_t)required_bytes - current_bytes;
    pages = (missing + bytes_per_page - 1) / bytes_per_page;

    return wasm_runtime_enlarge_memory((wasm_module_inst_t)instance, pages)
           ? 1 : 0;
}

int jani_wasm_instance_call(void *instance, void *exec_env, const char *name) {
    wasm_function_inst_t function;

    if ((instance == NULL) || (exec_env == NULL) || (name == NULL)) {
        return 0;
    }

    function = wasm_runtime_lookup_function((wasm_module_inst_t)instance, name);
    if (function == NULL) {
        printk("ERROR: wamr: module exports no '%s'\n", name);
        return 0;
    }

    if (!wasm_runtime_call_wasm((wasm_exec_env_t)exec_env, function, 0, NULL)) {
        printk("ERROR: wamr: call '%s' failed: %s\n", name,
               wasm_runtime_get_exception((wasm_module_inst_t)instance));
        return 0;
    }

    return 1;
}

int jani_wasm_run_module(const uint8_t *bytes, size_t length) {
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

    if (!jani_wasm_runtime_start()) {
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
    jani_wasm_runtime_stop();

    heap_after = kheap_used_bytes();
    printk("wamr: instance destroyed, heap high-water %d -> %d bytes\n",
           (int)heap_before, (int)heap_after);

    return result;
}