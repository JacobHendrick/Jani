#include <stdint.h>

#include "../lib/printk.h"
#include "memory_map.h"

#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_MEMMAP_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct memory_map_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = 0
};

uint64_t memory_map_entry_count(void) {
    struct limine_memmap_response *response = memmap_request.response;

    if (response == 0) {
        return 0;
    }

    return response->entry_count;
}

int memory_map_get_entry(uint64_t index, struct memory_map_entry *entry) {
    struct limine_memmap_response *response = memmap_request.response;
    struct memory_map_entry *source;

    if ((response == 0) || (entry == 0) ||
        (index >= response->entry_count)) {
        return 0;
    }

    source = response->entries[index];
    if (source == 0) {
        return 0;
    }

    *entry = *source;
    return 1;
}

void memory_map_print(void) {
    struct limine_memmap_response *response;
    uint64_t index;

    response = memmap_request.response;

    if (response == 0) {
        kputs("ERROR: Limine did not provide a memory map\n");
        return;
    }

    printk("memory map: %d entries\n", (int)response->entry_count);

    for (index = 0; index < response->entry_count; index++) {
        struct memory_map_entry *entry = response->entries[index];

        printk("region %d: base=%p end=%p type=%d\n",
            (int)index,
            (void *)(uintptr_t)entry->base,
            (void *)(uintptr_t)(entry->base + entry->length),
            (int)entry->type);

    }
}
