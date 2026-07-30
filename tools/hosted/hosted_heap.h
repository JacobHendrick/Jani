#ifndef JANI_TOOLS_HOSTED_HOSTED_HEAP_H
#define JANI_TOOLS_HOSTED_HOSTED_HEAP_H

#include <stdint.h>

/* The static arena standing in for physical memory in heap tests. */
uint64_t hosted_heap_arena_base(void);
uint64_t hosted_heap_arena_size(void);
void hosted_heap_reset(void);
uint64_t hosted_heap_mapped_pages(void);

#endif
