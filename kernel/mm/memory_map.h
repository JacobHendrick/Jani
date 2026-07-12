#ifndef JANI_KERNEL_MM_MEMORY_MAP_H
#define JANI_KERNEL_MM_MEMORY_MAP_H

#include <stdint.h>

#define MEMORY_MAP_USABLE 0ULL

struct memory_map_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

void memory_map_print(void);
uint64_t memory_map_entry_count(void);
int memory_map_get_entry(uint64_t index, struct memory_map_entry *entry);

#endif // JANI_KERNEL_MM_MEMORY_MAP_H
