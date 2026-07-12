#ifndef JANI_TOOLS_HOSTED_FAKE_MEMORY_MAP_H
#define JANI_TOOLS_HOSTED_FAKE_MEMORY_MAP_H

#include <stdint.h>

/* Scripts the machine's physical memory map for a test: reset, add
 * regions, then call pmm_init() - the real PMM reads whatever machine
 * you invented. Types come from kernel/mm/memory_map.h
 * (MEMORY_MAP_USABLE is 0; anything else is not usable). */
void fake_memory_map_reset(void);
int fake_memory_map_add(uint64_t base, uint64_t length, uint64_t type);

#endif
