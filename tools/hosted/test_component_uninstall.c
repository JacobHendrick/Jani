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
#define LEGACY_CAPTABLE_BYTES \
    (COMPONENT_CAPTABLE_HEADER_SIZE + \
     (COMPONENT_CAP_SLOTS * sizeof(struct capability)))
#define V2_CAPTABLE_BYTES \
    (LEGACY_CAPTABLE_BYTES + \
     (COMPONENT_CAP_SLOTS * sizeof(uint32_t)))

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

static int write_legacy_captable(
    struct object_store *store,
    const struct component *component
) {
    uint8_t payload[LEGACY_CAPTABLE_BYTES];
    struct component_captable_header header;

    memset(payload, 0, sizeof(payload));
    memcpy(payload + COMPONENT_CAPTABLE_HEADER_SIZE,
           component->capability_table.slots,
           sizeof(component->capability_table.slots));

    memset(&header, 0, sizeof(header));
    header.magic = COMPONENT_CAPTABLE_MAGIC;
    header.format_version = COMPONENT_CAPTABLE_FORMAT_VERSION_V1;
    header.slot_count = component->capability_count;
    header.next_object_sequence = component->next_object_sequence;
    header.payload_crc32c = object_crc32c(
        payload + COMPONENT_CAPTABLE_HEADER_SIZE,
        sizeof(component->capability_table.slots)
    );
    memcpy(payload, &header, sizeof(header));

    return object_store_put(
        store, component->captable_id,
        make_id(0, COMPONENT_TYPE_CAPTABLE), component->root_id,
        component->root_id, 0, payload, sizeof(payload)
    );
}

static int write_v2_captable(
    struct object_store *store,
    const struct component *component
) {
    uint8_t payload[V2_CAPTABLE_BYTES];
    struct component_captable_header header;

    memset(payload, 0, sizeof(payload));
    memcpy(payload + COMPONENT_CAPTABLE_HEADER_SIZE,
           component->capability_table.slots,
           sizeof(component->capability_table.slots));
    memcpy(payload + LEGACY_CAPTABLE_BYTES,
           component->capability_table.parents,
           sizeof(component->capability_table.parents));

    memset(&header, 0, sizeof(header));
    header.magic = COMPONENT_CAPTABLE_MAGIC;
    header.format_version = COMPONENT_CAPTABLE_FORMAT_VERSION_V2;
    header.slot_count = component->capability_count;
    header.next_object_sequence = component->next_object_sequence;
    header.payload_crc32c = object_crc32c(
        payload + COMPONENT_CAPTABLE_HEADER_SIZE,
        sizeof(component->capability_table.slots) +
            sizeof(component->capability_table.parents)
    );
    memcpy(payload, &header, sizeof(header));

    return object_store_put(
        store, component->captable_id,
        make_id(0, COMPONENT_TYPE_CAPTABLE), component->root_id,
        component->root_id, 0, payload, sizeof(payload)
    );
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
    struct component saved_caps;
    struct component loaded_caps;
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

    memset(&saved_caps, 0, sizeof(saved_caps));
    capability_table_init(&saved_caps.capability_table);
    saved_caps.root_id = root_id;
    saved_caps.captable_id = captable_id;
    saved_caps.capability_count = 3;
    saved_caps.next_object_sequence = 17;
    saved_caps.capability_table.slots[0].object = root_id;
    saved_caps.capability_table.slots[0].rights = COMPONENT_RIGHTS_READ |
                                                 COMPONENT_RIGHTS_GRANT;
    saved_caps.capability_table.slots[1].object = root_id;
    saved_caps.capability_table.slots[1].rights = COMPONENT_RIGHTS_READ |
                                                 COMPONENT_RIGHTS_GRANT;
    saved_caps.capability_table.slots[2].object = root_id;
    saved_caps.capability_table.slots[2].rights = COMPONENT_RIGHTS_READ;
    saved_caps.capability_table.parents[1] = 0;
    saved_caps.capability_table.parents[2] = 1;
    saved_caps.capability_table.generations[0] = 3;
    saved_caps.capability_table.generations[1] = 5;
    saved_caps.capability_table.generations[2] = 8;
    saved_caps.capability_table.generations[7] = 13;

    saved_caps.capability_table.generations[1] = 0;
    CHECK(!component_captable_write(&store, &saved_caps));
    saved_caps.capability_table.generations[1] = 5;
    CHECK(component_captable_write(&store, &saved_caps));
    CHECK(object_store_mount(&remounted, make_io(&disk), remounted_entries,
                             remounted_scratch, TABLE_CAPACITY,
                             remounted_cache, sizeof(remounted_cache),
                             remounted_bitmap, sizeof(remounted_bitmap),
                             remounted_arena, sizeof(remounted_arena)));
    memset(&loaded_caps, 0, sizeof(loaded_caps));
    loaded_caps.captable_id = captable_id;
    CHECK(component_captable_read(&remounted, &loaded_caps));
    CHECK(loaded_caps.capability_count == 3);
    CHECK(loaded_caps.next_object_sequence == 17);
    CHECK(loaded_caps.capability_table.parents[0] ==
          COMPONENT_CAP_PARENT_NONE);
    CHECK(loaded_caps.capability_table.parents[1] == 0);
    CHECK(loaded_caps.capability_table.parents[2] == 1);
    CHECK(loaded_caps.capability_table.slots[2].rights ==
          COMPONENT_RIGHTS_READ);
    CHECK(loaded_caps.capability_table.generations[0] == 3);
    CHECK(loaded_caps.capability_table.generations[1] == 5);
    CHECK(loaded_caps.capability_table.generations[2] == 8);
    CHECK(loaded_caps.capability_table.generations[7] == 13);

    saved_caps.next_object_sequence = 18;
    CHECK(write_v2_captable(&store, &saved_caps));
    memset(&loaded_caps, 0, sizeof(loaded_caps));
    loaded_caps.captable_id = captable_id;
    CHECK(component_captable_read(&store, &loaded_caps));
    CHECK(loaded_caps.next_object_sequence == 18);
    CHECK(loaded_caps.capability_table.parents[1] == 0);
    CHECK(loaded_caps.capability_table.parents[2] == 1);
    CHECK(loaded_caps.capability_table.generations[0] == 1);
    CHECK(loaded_caps.capability_table.generations[1] == 1);
    CHECK(loaded_caps.capability_table.generations[2] == 1);
    CHECK(loaded_caps.capability_table.generations[7] == 0);

    saved_caps.next_object_sequence = 19;
    CHECK(write_legacy_captable(&store, &saved_caps));
    memset(&loaded_caps, 0, sizeof(loaded_caps));
    loaded_caps.captable_id = captable_id;
    CHECK(component_captable_read(&store, &loaded_caps));
    CHECK(loaded_caps.next_object_sequence == 19);
    CHECK(loaded_caps.capability_table.parents[0] ==
          COMPONENT_CAP_PARENT_NONE);
    CHECK(loaded_caps.capability_table.parents[1] ==
          COMPONENT_CAP_PARENT_NONE);
    CHECK(loaded_caps.capability_table.parents[2] ==
          COMPONENT_CAP_PARENT_NONE);
    CHECK(loaded_caps.capability_table.generations[0] == 1);
    CHECK(loaded_caps.capability_table.generations[1] == 1);
    CHECK(loaded_caps.capability_table.generations[2] == 1);

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
