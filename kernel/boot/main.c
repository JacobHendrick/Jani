#include <stddef.h>
#include <stdint.h>

#include "../arch/gdt.h"
#include "../arch/idt.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include "../drivers/virtio_blk.h"
#include "../lib/printk.h"
#include "../mm/heap.h"
#include "../mm/layout.h"
#include "../mm/memory_map.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../obj/object_store.h"

#define LIMINE_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER { 0xadc0e0531bb10d03, 0x9572709f31764c62 }
#define LIMINE_BASE_REVISION(N) { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

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

#define DEMO_SECTORS 4096u
#define DEMO_TABLE_CAPACITY 64u
#define DEMO_CACHE_BYTES 8192u
#define DEMO_ARENA_BYTES 16384u
#define DEMO_BITMAP_BYTES ((DEMO_SECTORS + 7u) / 8u)

static struct object_store demo_store;
static struct object_table_entry demo_entries[DEMO_TABLE_CAPACITY];
static struct object_table_entry demo_scratch[DEMO_TABLE_CAPACITY];
static _Alignas(16) uint8_t demo_cache[DEMO_CACHE_BYTES];
static _Alignas(16) uint8_t demo_arena[DEMO_ARENA_BYTES];
static uint8_t demo_bitmap[DEMO_BITMAP_BYTES];

static int run_store_demo(void) {
    struct object_store_io io;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    struct object_id id;
    struct object_id type_id;
    struct object_id actor;
    uint64_t snapshot;
    const uint8_t first_payload[] = { 10, 20, 30 };
    const uint8_t second_payload[] = { 40, 50, 60, 70 };

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

    id.high = 0x1122334455667788ULL;
    id.low = 0x99AABBCCDDEEFF00ULL;
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

        if (!object_store_get(&demo_store, id, &header, &payload,
                              &payload_size)) {
            kputs("ERROR: mounted store lost the object\n");
            return 0;
        }

        printk("recovered across reboot: version %d, payload %d bytes\n",
               (int)header.version, (int)payload_size);
        return (header.version == 1) &&
               (payload_size == sizeof(first_payload));
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

    if (!object_store_put(&demo_store, id, type_id, actor, actor, 1,
                          first_payload, sizeof(first_payload))) {
        kputs("ERROR: store put v1 failed\n");
        return 0;
    }

    if (!object_store_snapshot_create(&demo_store, &snapshot)) {
        kputs("ERROR: snapshot create failed\n");
        return 0;
    }
    printk("snapshot taken: id %d\n", (int)snapshot);

    if (!object_store_put(&demo_store, id, type_id, actor, actor, 2,
                          second_payload, sizeof(second_payload))) {
        kputs("ERROR: store put v2 failed\n");
        return 0;
    }

    if (!object_store_get(&demo_store, id, &header, &payload, &payload_size)) {
        kputs("ERROR: store get after put v2 failed\n");
        return 0;
    }
    printk("after mutation: version %d, payload %d bytes\n",
           (int)header.version, (int)payload_size);

    if (!object_store_snapshot_rollback(&demo_store, snapshot)) {
        kputs("ERROR: rollback failed\n");
        return 0;
    }

    if (!object_store_get(&demo_store, id, &header, &payload, &payload_size)) {
        kputs("ERROR: store get after rollback failed\n");
        return 0;
    }
    printk("after rollback: version %d, payload %d bytes\n",
           (int)header.version, (int)payload_size);

    if ((header.version != 1) || (payload_size != sizeof(first_payload))) {
        kputs("ERROR: rollback did not restore version 1\n");
        return 0;
    }

    return 1;
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
    halt_forever();
}
