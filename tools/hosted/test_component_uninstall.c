#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_header.h"
#include "../../kernel/wasm/component.h"
#include "check.h"

#define DISK_SECTORS 256u
#define TABLE_CAPACITY 16u
#define CACHE_BYTES 8192u
#define BITMAP_BYTES ((DISK_SECTORS + 7u) / 8u)
#define ARENA_BYTES 16384u

unsigned long checks_passed;

struct hosted_disk {
    uint8_t bytes[DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    unsigned long writes_allowed;
};

static int disk_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct hosted_disk *disk = context;

    if ((disk == NULL) || (buffer == NULL) || (sector >= DISK_SECTORS)) {
        return 0;
    }

    memcpy(buffer, disk->bytes[sector], OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int disk_write(
    void *context,
    uint64_t sector,
    const uint8_t *buffer
) {
    struct hosted_disk *disk = context;

    if ((disk == NULL) || (buffer == NULL) || (sector >= DISK_SECTORS) ||
        (disk->writes_allowed == 0)) {
        return 0;
    }

    disk->writes_allowed--;
    memcpy(disk->bytes[sector], buffer, OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int disk_flush(void *context) {
    return context != NULL;
}

static struct object_store_io make_io(struct hosted_disk *disk) {
    struct object_store_io io;

    io.context = disk;
    io.sector_count = DISK_SECTORS;
    io.read_sector = disk_read;
    io.write_sector = disk_write;
    io.flush = disk_flush;
    return io;
}

static struct object_id make_id(uint64_t high, uint64_t low) {
    struct object_id id;

    id.high = high;
    id.low = low;
    return id;
}

static int write_root(
    struct object_store *store,
    struct object_id root_id,
    struct object_id module_id,
    struct object_id captable_id,
    struct object_id state_id
) {
    struct component_root_record record;

    memset(&record, 0, sizeof(record));
    record.magic = COMPONENT_ROOT_MAGIC;
    record.format_version = COMPONENT_ROOT_FORMAT_VERSION;
    record.module_id = module_id;
    record.captable_id = captable_id;
    record.state_id = state_id;
    record.payload_crc32c = object_crc32c(
        (const uint8_t *)&record.module_id,
        3 * sizeof(struct object_id)
    );

    return object_store_put(
        store,
        root_id,
        make_id(0, COMPONENT_TYPE_ROOT),
        make_id(0, 0),
        make_id(0, 0),
        0,
        (const uint8_t *)&record,
        sizeof(record)
    );
}

static void test_uninstall_removes_only_install_state(void) {
    struct hosted_disk disk;
    struct object_store store;
    struct object_store remounted;
    struct object_table_entry entries[TABLE_CAPACITY];
    struct object_table_entry scratch[TABLE_CAPACITY];
    struct object_table_entry remounted_entries[TABLE_CAPACITY];
    struct object_table_entry remounted_scratch[TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[CACHE_BYTES];
    uint8_t bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t arena[ARENA_BYTES];
    _Alignas(16) uint8_t remounted_cache[CACHE_BYTES];
    uint8_t remounted_bitmap[BITMAP_BYTES];
    _Alignas(16) uint8_t remounted_arena[ARENA_BYTES];
    struct object_id roots[COMPONENT_MAX];
    struct object_id root_id;
    struct object_id module_id;
    struct object_id captable_id;
    struct object_id state_id;
    struct object_id data_id;
    struct object_header header;
    const uint8_t *payload;
    size_t payload_size;
    size_t count;
    uint64_t sequence;
    struct component resumed;
    const uint8_t module[] = { 0x00, 0x61, 0x73, 0x6D };
    const uint8_t data[] = { 7, 8, 9 };

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = ULONG_MAX;
    root_id = make_id(2, 100);
    module_id = make_id(2, 101);
    captable_id = make_id(2, 102);
    state_id = make_id(2, 103);
    data_id = make_id(3, 1);

    CHECK(object_store_format(&store, make_io(&disk), entries, scratch,
                              TABLE_CAPACITY, cache, sizeof(cache), bitmap,
                              sizeof(bitmap), arena, sizeof(arena)));
    CHECK(object_store_put(&store, module_id,
                           make_id(0, COMPONENT_TYPE_MODULE), root_id,
                           root_id, 0, module, sizeof(module)));
    CHECK(write_root(&store, root_id, module_id, captable_id, state_id));
    CHECK(object_store_put(&store, data_id,
                           make_id(0, COMPONENT_TYPE_DATA), root_id,
                           root_id, 1, data, sizeof(data)));

    roots[0] = root_id;
    CHECK(component_registry_store(&store, roots, 1, 200));
    CHECK(component_uninstall(&store, root_id));

    CHECK(component_registry_load(&store, roots, COMPONENT_MAX, &count,
                                  &sequence));
    CHECK(count == 0);
    CHECK(sequence == 200);
    CHECK(!object_store_get(&store, module_id, &header, &payload,
                            &payload_size));
    CHECK(object_store_get(&store, root_id, &header, &payload,
                           &payload_size));
    CHECK(object_store_get(&store, data_id, &header, &payload,
                           &payload_size));
    CHECK(payload_size == sizeof(data));
    CHECK(payload[0] == 7);
    CHECK(!component_resume(&store, root_id, &resumed));
    CHECK(!component_uninstall(&store, root_id));
    CHECK(!component_uninstall(&store, make_id(0, 0)));
    CHECK(!component_uninstall(NULL, root_id));

    CHECK(object_store_mount(&remounted, make_io(&disk), remounted_entries,
                             remounted_scratch, TABLE_CAPACITY,
                             remounted_cache, sizeof(remounted_cache),
                             remounted_bitmap, sizeof(remounted_bitmap),
                             remounted_arena, sizeof(remounted_arena)));
    CHECK(component_registry_load(&remounted, roots, COMPONENT_MAX, &count,
                                  &sequence));
    CHECK(count == 0);
    CHECK(!object_store_get(&remounted, module_id, &header, &payload,
                            &payload_size));
    CHECK(object_store_get(&remounted, root_id, &header, &payload,
                           &payload_size));
    CHECK(object_store_get(&remounted, data_id, &header, &payload,
                           &payload_size));
}

int jani_wasm_instance_create(
    const uint8_t *bytes,
    size_t length,
    void **module_out,
    void **instance_out,
    void **exec_env_out,
    void **owned_bytes_out
) {
    (void)bytes;
    (void)length;
    (void)module_out;
    (void)instance_out;
    (void)exec_env_out;
    (void)owned_bytes_out;
    return 0;
}

void jani_wasm_instance_destroy(
    void *module,
    void *instance,
    void *exec_env,
    void *owned_bytes
) {
    (void)module;
    (void)instance;
    (void)exec_env;
    (void)owned_bytes;
}

int jani_wasm_instance_memory(
    void *instance,
    uint8_t **base_out,
    size_t *size_out
) {
    (void)instance;
    (void)base_out;
    (void)size_out;
    return 0;
}

int jani_wasm_instance_memory_grow(void *instance, size_t required_bytes) {
    (void)instance;
    (void)required_bytes;
    return 0;
}

int jani_wasm_instance_call(void *instance, void *exec_env, const char *name) {
    (void)instance;
    (void)exec_env;
    (void)name;
    return 0;
}

void jani_wasm_set_current_component(struct component *component) {
    (void)component;
}

void *kmalloc(size_t size) {
    (void)size;
    return NULL;
}

void kfree(void *memory) {
    (void)memory;
}

int main(void) {
    checks_passed = 0;

    test_uninstall_removes_only_install_state();

    printf("test_component_uninstall: %lu checks passed\n", checks_passed);
    return 0;
}
