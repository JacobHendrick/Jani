#include <stddef.h>
#include <stdint.h>

#include "../arch/gdt.h"
#include "../arch/idt.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include "../drivers/virtio_blk.h"
#include "../lib/printk.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../mm/layout.h"
#include "../mm/memory_map.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../obj/object_store.h"
#include "../wasm/component.h"
#include "../wasm/instance_state.h"
#include "../wasm/module.h"
#include "../wasm/runtime.h"
#include "../arch/stack.h"

#define LIMINE_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER { 0xadc0e0531bb10d03, 0x9572709f31764c62 }
#define LIMINE_BASE_REVISION(N) { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_STACK_SIZE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x224ef0460a8e8926, 0xe1cb0fc25f46ea3d }

struct limine_stack_size_response {
    uint64_t revision;
};

struct limine_stack_size_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_stack_size_response *response;
    uint64_t stack_size;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST,
    .revision = 0,
    .response = 0,
    .stack_size = 256 * 1024
};

#define LIMINE_MODULE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }

struct limine_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct limine_file {
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *string;
    uint32_t media_type;
    uint32_t unused;
    uint8_t tftp_ipv4[4];
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct limine_uuid gpt_disk_uuid;
    struct limine_uuid gpt_partition_uuid;
    struct limine_uuid part_uuid;
};

struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;
};

struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

#define FAULT_TEST_NONE 0
#define FAULT_TEST_INVALID_OPCODE 1
#define FAULT_TEST_PAGE_FAULT 2
#define FAULT_TEST_OBJECT_SPACE 3

#define FAULT_TEST_MODE FAULT_TEST_NONE

static void run_fault_test(void) {
#if FAULT_TEST_MODE == FAULT_TEST_INVALID_OPCODE
    kputs("triggering invalid opcode test\n");
    __asm__ volatile ("ud2");
#elif FAULT_TEST_MODE == FAULT_TEST_PAGE_FAULT
    kputs("triggering page fault test\n");
    __asm__ volatile (
        "xorq %%rax, %%rax\n"
        "movq (%%rax), %%rax\n"
        :
        :
        : "rax", "memory"
    );
#elif FAULT_TEST_MODE == FAULT_TEST_OBJECT_SPACE
    kputs("triggering object-space fault test\n");
    {
        volatile uint64_t *object_space_pointer;

        object_space_pointer = (volatile uint64_t *)(uintptr_t)
            (MEMORY_LAYOUT_OBJECT_SPACE_BASE + 0x1000);
        (void)*object_space_pointer;
    }
#endif
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

static int run_heap_test(void) {
    uint8_t *small_block;
    uint8_t *large_block;
    uint8_t *reused_block;
    uint8_t *grown_block;
    uint8_t *shrunk_block;
    size_t index;

    kheap_init(MEMORY_LAYOUT_HEAP_BASE, MEMORY_LAYOUT_HEAP_SIZE);

    small_block = kmalloc(32);
    large_block = kmalloc(5000);

    if ((small_block == 0) || (large_block == 0) ||
        (((uintptr_t)small_block % 16) != 0) ||
        (((uintptr_t)large_block % 16) != 0)) {
        return 0;
    }

    small_block[0] = 0xA5;
    small_block[31] = 0x5A;
    for (index = 0; index < 5000; index++) {
        large_block[index] = (uint8_t)(index ^ 0x5A);
    }

    if ((small_block[0] != 0xA5) || (small_block[31] != 0x5A) ||
        (large_block[0] != (uint8_t)(0 ^ 0x5A)) ||
        (large_block[4999] != (uint8_t)(4999 ^ 0x5A))) {
        return 0;
    }

    kfree(small_block);
    reused_block = kmalloc(16);

    if (reused_block != small_block) {
        return 0;
    }

    reused_block[0] = 0x3C;
    reused_block[15] = 0xC3;
    if ((reused_block[0] != 0x3C) || (reused_block[15] != 0xC3)) {
        return 0;
    }
    kputs("heap free-list reuse test ok\n");

    grown_block = krealloc(large_block, 7000);
    if (grown_block == NULL) {
        return 0;
    }

    for (index = 0; index < 5000; index++) {
        if (grown_block[index] != (uint8_t)(index ^ 0x5A)) {
            return 0;
        }
    }
    kputs("heap realloc growth preservation test ok\n");

    shrunk_block = krealloc(grown_block, 1024);
    if (shrunk_block != grown_block) {
        return 0;
    }

    for (index = 0; index < 1024; index++) {
        if (shrunk_block[index] != (uint8_t)(index ^ 0x5A)) {
            return 0;
        }
    }
    kputs("heap realloc shrink-in-place test ok\n");

    kfree(reused_block);
    kfree(shrunk_block);

    printk("heap test used: %d bytes\n", (int)kheap_used_bytes());
    return 1;
}

#define DEMO_SECTORS 16384u
#define DEMO_TABLE_CAPACITY 64u
#define DEMO_CACHE_BYTES 98304u
#define DEMO_ARENA_BYTES 16384u
#define DEMO_BITMAP_BYTES ((DEMO_SECTORS + 7u) / 8u)

#define DEMO_OBJECTS 4u
#define DEMO_PAYLOAD_BYTES 48u
#define DEMO_WRITE_ROUNDS 400u
#define DEMO_COLLECT_EVERY 64u

static struct object_store demo_store;
static struct object_table_entry demo_entries[DEMO_TABLE_CAPACITY];
static struct object_table_entry demo_scratch[DEMO_TABLE_CAPACITY];
static _Alignas(16) uint8_t demo_cache[DEMO_CACHE_BYTES];
static _Alignas(16) uint8_t demo_arena[DEMO_ARENA_BYTES];
static uint8_t demo_bitmap[DEMO_BITMAP_BYTES];
static uint8_t demo_payload[DEMO_PAYLOAD_BYTES];

static struct object_id demo_object_id(unsigned index) {
    struct object_id id;

    id.high = 0x1122334455667788ULL;
    id.low = 0x99AABBCCDDEE0000ULL + (uint64_t)index;
    return id;
}

static void demo_fill_payload(uint8_t *buffer, uint64_t version) {
    size_t index;

    for (index = 0; index < DEMO_PAYLOAD_BYTES; index++) {
        buffer[index] = (uint8_t)((version * 31u) + (index * 7u) + 5u);
    }
}

static int demo_payload_matches(
    const uint8_t *actual,
    size_t actual_size,
    uint64_t version
) {
    size_t index;

    if (actual_size != DEMO_PAYLOAD_BYTES) {
        return 0;
    }

    for (index = 0; index < DEMO_PAYLOAD_BYTES; index++) {
        if (actual[index] !=
            (uint8_t)((version * 31u) + (index * 7u) + 5u)) {
            return 0;
        }
    }

    return 1;
}

static int demo_owns_object(struct object_id id) {
    unsigned slot;

    for (slot = 0; slot < DEMO_OBJECTS; slot++) {
        if (object_id_equal(id, demo_object_id(slot))) {
            return 1;
        }
    }

    return 0;
}

static int demo_verify_all(void) {
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    size_t index;

    for (index = 0; index < demo_store.table.count; index++) {
        struct object_id id = demo_store.table.entries[index].id;
        uint64_t expected_version = demo_store.table.entries[index].version;

        if (!object_store_get(&demo_store, id, &header, &payload,
                              &payload_size)) {
            printk("ERROR: entry %d is not readable\n", (int)index);
            return 0;
        }
        if (header.version != expected_version) {
            printk("ERROR: entry %d version %d != table %d\n", (int)index,
                   (int)header.version, (int)expected_version);
            return 0;
        }
        if (!object_id_equal(header.id, id)) {
            printk("ERROR: entry %d carries the wrong id\n", (int)index);
            return 0;
        }
        if (demo_owns_object(id) &&
            !demo_payload_matches(payload, payload_size, header.version)) {
            printk("ERROR: entry %d payload does not match version %d\n",
                   (int)index, (int)header.version);
            return 0;
        }
    }

    return 1;
}

static int demo_write_workload(void) {
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id type_id;
    struct object_id actor;
    unsigned round;
    unsigned slot;

    type_id.high = 1;
    type_id.low = 1;
    actor.high = 2;
    actor.low = 2;

    for (round = 0; round < DEMO_WRITE_ROUNDS; round++) {
        for (slot = 0; slot < DEMO_OBJECTS; slot++) {
            struct object_id id = demo_object_id(slot);
            uint64_t next_version = 1;

            if (object_store_get(&demo_store, id, &header, &payload,
                                 &payload_size)) {
                next_version = header.version + 1;
            }

            demo_fill_payload(demo_payload, next_version);

            if (!object_store_put(&demo_store, id, type_id, actor, actor,
                                  round, demo_payload,
                                  DEMO_PAYLOAD_BYTES)) {
                printk("ERROR: put failed at round %d slot %d\n", (int)round,
                       (int)slot);
                return 0;
            }
        }

        if ((round % DEMO_COLLECT_EVERY) == (DEMO_COLLECT_EVERY - 1u)) {
            if (!object_store_collect(&demo_store)) {
                printk("ERROR: collect failed at round %d\n", (int)round);
                return 0;
            }
        }

        if ((round % 25u) == 0) {
            printk("workload round %d\n", (int)round);
        }
    }

    kputs("workload complete\n");
    return 1;
}

static int run_store_demo(void) {
    struct object_store_io io;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id id;
    struct object_id type_id;
    struct object_id actor;
    uint64_t snapshot;

    if (!virtio_blk_init()) {
        return 0;
    }

    io.context = 0;
    io.sector_count = virtio_blk_capacity_sectors();
    if (io.sector_count > DEMO_SECTORS) {
        io.sector_count = DEMO_SECTORS;
    }
    io.read_sector = virtio_blk_io_read;
    io.write_sector = virtio_blk_io_write;
    io.flush = virtio_blk_io_flush;

    id = demo_object_id(0);
    type_id.high = 1;
    type_id.low = 1;
    actor.high = 2;
    actor.low = 2;

    if (object_store_mount(&demo_store, io, demo_entries, demo_scratch,
                           DEMO_TABLE_CAPACITY, demo_cache,
                           sizeof(demo_cache), demo_bitmap,
                           sizeof(demo_bitmap), demo_arena,
                           sizeof(demo_arena))) {
        kputs("store mounted from an existing disk\n");

        if (!demo_verify_all()) {
            kputs("RECOVERY FAILED\n");
            return 0;
        }

        printk("RECOVERY OK: %d objects consistent\n",
               (int)demo_store.table.count);
        return demo_write_workload();
    }

    kputs("no valid store on disk; formatting\n");

    if (!object_store_format(&demo_store, io, demo_entries, demo_scratch,
                             DEMO_TABLE_CAPACITY, demo_cache,
                             sizeof(demo_cache), demo_bitmap,
                             sizeof(demo_bitmap), demo_arena,
                             sizeof(demo_arena))) {
        kputs("ERROR: store format failed\n");
        return 0;
    }
    kputs("store format ok\n");

    demo_fill_payload(demo_payload, 1);
    if (!object_store_put(&demo_store, id, type_id, actor, actor, 1,
                          demo_payload, DEMO_PAYLOAD_BYTES)) {
        kputs("ERROR: store put v1 failed\n");
        return 0;
    }

    if (!object_store_snapshot_create(&demo_store, &snapshot)) {
        kputs("ERROR: snapshot create failed\n");
        return 0;
    }
    printk("snapshot taken: id %d\n", (int)snapshot);

    demo_fill_payload(demo_payload, 2);
    if (!object_store_put(&demo_store, id, type_id, actor, actor, 2,
                          demo_payload, DEMO_PAYLOAD_BYTES)) {
        kputs("ERROR: store put v2 failed\n");
        return 0;
    }

    if (!object_store_get(&demo_store, id, &header, &payload, &payload_size)) {
        kputs("ERROR: store get after put v2 failed\n");
        return 0;
    }
    if ((header.version != 2) ||
        !demo_payload_matches(payload, payload_size, 2)) {
        kputs("ERROR: mutation did not produce version 2\n");
        return 0;
    }
    printk("after mutation: version %d\n", (int)header.version);

    if (!object_store_snapshot_rollback(&demo_store, snapshot)) {
        kputs("ERROR: rollback failed\n");
        return 0;
    }

    if (!object_store_get(&demo_store, id, &header, &payload, &payload_size)) {
        kputs("ERROR: store get after rollback failed\n");
        return 0;
    }
    if ((header.version != 1) ||
        !demo_payload_matches(payload, payload_size, 1)) {
        kputs("ERROR: rollback did not restore version 1\n");
        return 0;
    }
    printk("after rollback: version %d\n", (int)header.version);

    if (!object_store_snapshot_discard(&demo_store, snapshot)) {
        kputs("ERROR: snapshot discard failed\n");
        return 0;
    }

    return demo_write_workload();
}

#define COMPONENT_TICK_PERIOD 100u
#define COMPONENT_COLLECT_EVERY 8u

#ifndef JANI_WOW_BUG
#define JANI_WOW_BUG 0
#endif

#define JANI_WOW_BUG_INIT_ON_RESUME 1
#define JANI_WOW_BUG_SKIP_COMMIT 2
#define JANI_WOW_BUG_LOSE_MEMORY 3

static struct component demo_component;
static int demo_component_ready;

static int run_component_leak_test(
    const uint8_t *module_bytes,
    size_t module_length
) {
    struct component probe;
    uint64_t after_first;
    uint64_t after_second;

    if (!jani_wasm_instance_create(module_bytes, module_length, &probe.module,
                                   &probe.instance, &probe.exec_env,
                                   &probe.module_bytes)) {
        kputs("ERROR: leak test could not instantiate\n");
        return 0;
    }
    jani_wasm_instance_destroy(probe.module, probe.instance, probe.exec_env,
                               probe.module_bytes);
    after_first = kheap_used_bytes();

    if (!jani_wasm_instance_create(module_bytes, module_length, &probe.module,
                                   &probe.instance, &probe.exec_env,
                                   &probe.module_bytes)) {
        kputs("ERROR: leak test could not reinstantiate\n");
        return 0;
    }
    jani_wasm_instance_destroy(probe.module, probe.instance, probe.exec_env,
                               probe.module_bytes);
    after_second = kheap_used_bytes();

    if (after_second != after_first) {
        printk("ERROR: instantiate leaked %d bytes on the second pass\n",
               (int)(after_second - after_first));
        return 0;
    }

    printk("component leak test ok (high-water steady at %d bytes)\n",
           (int)after_second);
    return 1;
}

static int run_component_demo(void) {
    struct object_id roots[COMPONENT_MAX];
    uint64_t sequence;
    size_t count;

    if (!jani_wasm_runtime_start()) {
        return 0;
    }

    if (component_registry_load(
            &demo_store,
            roots,
            COMPONENT_MAX,
            &count,
            &sequence
        ) && (count > 0)) {
        printk("store: registry found (%d component)\n", (int)count);

        if (!component_resume(&demo_store, roots[0], &demo_component)) {
            kputs("ERROR: component resume failed\n");
            return 0;
        }

        printk(
            "component: resumed at logical time %d\n",
            (int)demo_component.logical_time
        );

#if JANI_WOW_BUG == JANI_WOW_BUG_INIT_ON_RESUME
        (void)jani_wasm_instance_call(
            demo_component.instance,
            demo_component.exec_env,
            "jani_init"
        );
#elif JANI_WOW_BUG == JANI_WOW_BUG_LOSE_MEMORY
        {
            uint8_t *memory;
            size_t memory_size;

            if (jani_wasm_instance_memory(
                    demo_component.instance,
                    &memory,
                    &memory_size
                )) {
                memset(memory, 0, memory_size);
            }
        }
#endif
    } else {
        const uint8_t *module_bytes;
        size_t module_length;
        uint32_t section_count;

        if ((module_request.response == NULL) ||
            (module_request.response->module_count == 0)) {
            kputs(
                "ERROR: no component is installed and no module was supplied\n"
            );
            return 0;
        }

        module_bytes =
            (const uint8_t *)module_request.response->modules[0]->address;
        module_length =
            (size_t)module_request.response->modules[0]->size;

        section_count = 0;
        if (!jani_wasm_module_validate(
                module_bytes,
                module_length,
                &section_count
            )) {
            kputs("ERROR: wasm module failed pre-validation\n");
            return 0;
        }

        if (!run_component_leak_test(module_bytes, module_length)) {
            return 0;
        }

        kputs("store: no component installed\n");

        if (!component_install(
                &demo_store,
                module_bytes,
                module_length,
                &demo_component
            )) {
            kputs("ERROR: component install failed\n");
            return 0;
        }

        kputs("component: initialized\n");
    }

    demo_component_ready = 1;
    return 1;
}

static void component_tick_forever(void) {
    uint64_t next_deadline;

    next_deadline = pit_get_ticks() + COMPONENT_TICK_PERIOD;

    for (;;) {
        __asm__ volatile ("sti; hlt");

        if (!demo_component_ready || !demo_component.timer_armed) {
            continue;
        }

        if (pit_get_ticks() < next_deadline) {
            continue;
        }
        next_deadline += COMPONENT_TICK_PERIOD;

        if (!component_invoke_timer(&demo_component)) {
            kputs("ERROR: component timer handler failed\n");
            demo_component_ready = 0;
            continue;
        }

#if JANI_WOW_BUG != JANI_WOW_BUG_SKIP_COMMIT
        if (!component_commit(&demo_store, &demo_component)) {
            kputs("ERROR: component commit failed\n");
            demo_component_ready = 0;
            continue;
        }
#endif

        if ((demo_component.logical_time % COMPONENT_COLLECT_EVERY) == 0) {
            if (!object_store_collect(&demo_store)) {
                kputs("ERROR: component store collect failed\n");
                demo_component_ready = 0;
            }
        }
    }
}

void kmain(void) {
    uint64_t free_frames_before_test;
    uint64_t test_frame;

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    gdt_init();
    idt_init();
    serial_init();

    if (stack_size_request.response == 0) {
        kputs("WARNING: limine ignored the stack size request\n");
    } else {
        kernel_stack_set_size(256 * 1024);
        printk("stack: 256 KiB granted, limit %p\n",
               (void *)kernel_stack_limit());
    }

    {
        volatile double a = 2.0;
        volatile double b = 3.5;
        volatile double product = a * b;

        if ((product > 6.9) && (product < 7.1)) {
            kputs("fpu: floating point works\n");
        } else {
            kputs("ERROR: floating point produced a wrong result\n");
        }
    }

    memory_map_print();
    pmm_init();
    vmm_init();
    memory_layout_print();

    if (!vmm_is_ready()) {
        kputs("ERROR: VMM initialization failed\n");
    } else if (vmm_self_test()) {
        kputs("vmm map/translate/unmap test ok\n");

        if (run_heap_test()) {
            kputs("heap bare-metal allocator test ok\n");
        } else {
            kputs("ERROR: heap self-test failed\n");
        }

        if (run_store_demo()) {
            kputs("object store on virtio-blk ok\n");
        } else {
            kputs("ERROR: object store demo failed\n");
        }

        if (run_component_demo()) {
            kputs("component demo ok\n");
        } else {
            kputs("ERROR: component demo failed\n");
        }
    } else {
        kputs("ERROR: VMM self-test failed\n");
    }

    free_frames_before_test = pmm_get_free_frame_count();
    test_frame = pmm_alloc_frame();

    if (test_frame == 0) {
        kputs("ERROR: PMM could not allocate a test frame\n");
    } else {
        printk("pmm test frame: %p\n", (void *)(uintptr_t)test_frame);
        pmm_free_frame(test_frame);

        if (pmm_get_free_frame_count() == free_frames_before_test) {
            kputs("pmm allocate/free test ok\n");
        } else {
            kputs("ERROR: PMM free count did not recover\n");
        }
    }

    pic_remap(PIC1_VECTOR_OFFSET, PIC2_VECTOR_OFFSET);
    pic_mask_all();
    pic_send_eoi(0);
    pit_init(100);
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_send_eoi(0);

    kputs("gdt init ok\n");
    kputs("idt init ok\n");
    kputs("serial init ok\n");
    kputs("pic init ok\n");
    kputs("pit init ok\n");
    kputs("keyboard irq ready\n");
    printk("hello from %s\n", "Jani OS");
    printk("format check: %d %x %p\n", 42, 0x2a, (void *)limine_base_revision);

    run_fault_test();

    kputs("interrupts enabled\n");

    if (demo_component_ready) {
        component_tick_forever();
    }

    halt_forever();
}
