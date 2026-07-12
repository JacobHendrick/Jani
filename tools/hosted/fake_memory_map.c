#include <stdint.h>

#include "../../kernel/mm/memory_map.h"
#include "fake_memory_map.h"

#define FAKE_MEMORY_MAP_MAX_ENTRIES 32

static struct memory_map_entry entries[FAKE_MEMORY_MAP_MAX_ENTRIES];
static uint64_t entry_count;

void fake_memory_map_reset(void) {
    entry_count = 0;
}

int fake_memory_map_add(uint64_t base, uint64_t length, uint64_t type) {
    if (entry_count >= FAKE_MEMORY_MAP_MAX_ENTRIES) {
        return 0;
    }

    entries[entry_count].base = base;
    entries[entry_count].length = length;
    entries[entry_count].type = type;
    entry_count++;
    return 1;
}

/* The two functions below ARE the kernel's memory_map interface - the
 * same prototypes memory_map.c implements on real hardware. The PMM
 * cannot tell the difference; that seam is what makes the hosted twin
 * possible. */
uint64_t memory_map_entry_count(void) {
    return entry_count;
}

int memory_map_get_entry(uint64_t index, struct memory_map_entry *entry) {
    if (index >= entry_count) {
        return 0;
    }

    *entry = entries[index];
    return 1;
}
