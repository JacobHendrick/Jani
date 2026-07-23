#include <stddef.h>
#include <stdint.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_store.h"

#define FUZZ_DISK_SECTORS 256u
#define FUZZ_TABLE_CAPACITY 6u
#define FUZZ_CACHE_BYTES 4096u
#define FUZZ_BITMAP_BYTES ((FUZZ_DISK_SECTORS + 7u) / 8u)
#define FUZZ_ID_COUNT 6u
#define FUZZ_PAYLOAD_LEN 16u

struct fuzz_disk {
    uint8_t bytes[FUZZ_DISK_SECTORS][OBJECT_STORE_SECTOR_SIZE];
    unsigned long writes_allowed;
};

static int fuzz_read(void *context, uint64_t sector, uint8_t *buffer) {
    struct fuzz_disk *disk = context;

    if ((sector >= FUZZ_DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    memcpy(buffer, disk->bytes[sector], OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int fuzz_write(void *context, uint64_t sector, const uint8_t *buffer) {
    struct fuzz_disk *disk = context;

    if ((sector >= FUZZ_DISK_SECTORS) || (buffer == NULL)) {
        return 0;
    }

    if (disk->writes_allowed == 0) {
        return 0;
    }
    disk->writes_allowed--;

    memcpy(disk->bytes[sector], buffer, OBJECT_STORE_SECTOR_SIZE);
    return 1;
}

static int fuzz_flush(void *context) {
    return context != NULL;
}

static struct object_store_io fuzz_io(struct fuzz_disk *disk) {
    struct object_store_io io;

    io.context = disk;
    io.sector_count = FUZZ_DISK_SECTORS;
    io.read_sector = fuzz_read;
    io.write_sector = fuzz_write;
    io.flush = fuzz_flush;
    return io;
}

static struct object_id fuzz_object_id(unsigned index) {
    struct object_id id;

    id.high = (uint64_t)index + 1u;
    id.low = 0x100u + index;
    return id;
}

static struct object_id fuzz_type_id(void) {
    struct object_id id;

    id.high = 2u;
    id.low = 1u;
    return id;
}

static void fuzz_fill_payload(uint8_t *payload, unsigned index, uint64_t version) {
    unsigned offset;

    for (offset = 0; offset < FUZZ_PAYLOAD_LEN; offset++) {
        payload[offset] =
            (uint8_t)((index * 31u) + (version * 7u) + offset);
    }
}

struct fuzz_store {
    struct object_store store;
    struct object_table_entry entries[FUZZ_TABLE_CAPACITY];
    struct object_table_entry scratch[FUZZ_TABLE_CAPACITY];
    _Alignas(16) uint8_t cache[FUZZ_CACHE_BYTES];
    uint8_t bitmap[FUZZ_BITMAP_BYTES];
};

static int fuzz_mount(struct fuzz_store *handle, struct fuzz_disk *disk) {
    return object_store_mount(&handle->store, fuzz_io(disk), handle->entries,
                              handle->scratch, FUZZ_TABLE_CAPACITY,
                              handle->cache, sizeof(handle->cache),
                              handle->bitmap, sizeof(handle->bitmap));
}

static unsigned fuzz_index_of(struct object_id id) {
    unsigned index;

    for (index = 0; index < FUZZ_ID_COUNT; index++) {
        if (object_id_equal(fuzz_object_id(index), id)) {
            return index;
        }
    }
    return FUZZ_ID_COUNT;
}

static void fuzz_verify_consistent(struct fuzz_store *handle) {
    size_t entry_index;

    for (entry_index = 0; entry_index < handle->store.table.count;
         entry_index++) {
        const struct object_table_entry *entry;
        struct object_header header;
        const uint8_t *payload;
        size_t payload_size;
        uint8_t expected[FUZZ_PAYLOAD_LEN];
        unsigned index;

        entry = &handle->store.table.entries[entry_index];

        if (entry_index != 0) {
            if (object_id_compare(
                    handle->store.table.entries[entry_index - 1].id,
                    entry->id) >= 0) {
                __builtin_trap();
            }
        }

        if (!object_store_get(&handle->store, entry->id, &header, &payload,
                              &payload_size)) {
            __builtin_trap();
        }

        if (!object_id_equal(header.id, entry->id) ||
            (header.version != entry->version) ||
            (payload_size != FUZZ_PAYLOAD_LEN)) {
            __builtin_trap();
        }

        index = fuzz_index_of(entry->id);
        if (index == FUZZ_ID_COUNT) {
            __builtin_trap();
        }

        fuzz_fill_payload(expected, index, header.version);
        for (payload_size = 0; payload_size < FUZZ_PAYLOAD_LEN;
             payload_size++) {
            if (payload[payload_size] != expected[payload_size]) {
                __builtin_trap();
            }
        }
    }
}

static uint64_t fuzz_pick_snapshot(struct fuzz_store *handle, uint8_t selector) {
    uint64_t ids[OBJECT_STORE_SNAPSHOT_LIMIT];
    size_t count;
    size_t slot;

    count = 0;
    for (slot = 0; slot < OBJECT_STORE_SNAPSHOT_LIMIT; slot++) {
        if (handle->store.snapshots[slot].id != 0) {
            ids[count++] = handle->store.snapshots[slot].id;
        }
    }

    if (count == 0) {
        return 0;
    }

    return ids[selector % count];
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    struct fuzz_disk disk;
    struct fuzz_store handle;
    size_t cursor;

    memset(&disk, 0, sizeof(disk));
    disk.writes_allowed = (unsigned long)-1;

    if (!object_store_format(&handle.store, fuzz_io(&disk), handle.entries,
                             handle.scratch, FUZZ_TABLE_CAPACITY,
                             handle.cache, sizeof(handle.cache),
                             handle.bitmap, sizeof(handle.bitmap))) {
        return 0;
    }

    cursor = 0;
    while ((size - cursor) >= 4) {
        uint8_t operation = data[cursor++] % 6u;
        uint8_t argument = data[cursor++];
        uint8_t budget_byte = data[cursor++];
        uint8_t selector = data[cursor++];
        uint64_t snapshot_id;

        disk.writes_allowed = (unsigned long)-1;
        if (!fuzz_mount(&handle, &disk)) {
            __builtin_trap();
        }
        fuzz_verify_consistent(&handle);

        if (budget_byte == 0) {
            disk.writes_allowed = (unsigned long)-1;
        } else {
            disk.writes_allowed = budget_byte;
        }

        if (operation <= 1u) {
            unsigned index = argument % FUZZ_ID_COUNT;
            const struct object_table_entry *existing;
            uint64_t next_version;
            uint8_t payload[FUZZ_PAYLOAD_LEN];

            existing = object_table_find(&handle.store.table,
                                         fuzz_object_id(index));
            next_version = (existing == NULL) ? 1u : existing->version + 1u;
            fuzz_fill_payload(payload, index, next_version);
            (void)object_store_put(&handle.store, fuzz_object_id(index),
                                   fuzz_type_id(), fuzz_object_id(0),
                                   fuzz_object_id(0), next_version, payload,
                                   sizeof(payload));
        } else if (operation == 2u) {
            uint64_t created;
            (void)object_store_snapshot_create(&handle.store, &created);
        } else if (operation == 3u) {
            snapshot_id = fuzz_pick_snapshot(&handle, selector);
            if (snapshot_id != 0) {
                (void)object_store_snapshot_rollback(&handle.store,
                                                     snapshot_id);
            }
        } else if (operation == 4u) {
            snapshot_id = fuzz_pick_snapshot(&handle, selector);
            if (snapshot_id != 0) {
                (void)object_store_snapshot_discard(&handle.store,
                                                    snapshot_id);
            }
        } else {
            (void)object_store_snapshot_prune(&handle.store,
                                              selector % 4u);
        }
    }

    disk.writes_allowed = (unsigned long)-1;
    if (!fuzz_mount(&handle, &disk)) {
        __builtin_trap();
    }
    fuzz_verify_consistent(&handle);

    return 0;
}
