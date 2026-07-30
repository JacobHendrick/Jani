/* Hosted page source for the heap (the Linux twin of heap_backend.c):
 * a static page-aligned arena plays the role of all physical memory.
 * "Mapping" a page is just admission control - the memory already
 * exists - and addresses outside the arena are refused, which is how
 * tests simulate running out of physical frames. */
#include <stdint.h>

#include "../../kernel/mm/heap.h"
#include "hosted_heap.h"

#define ARENA_PAGE_SIZE 4096ULL
#define ARENA_PAGE_COUNT 4096ULL /* 16 MiB */

static _Alignas(4096) uint8_t arena[ARENA_PAGE_SIZE * ARENA_PAGE_COUNT];
static uint64_t mapped_pages;

uint64_t hosted_heap_arena_base(void) {
    return (uint64_t)(uintptr_t)arena;
}

uint64_t hosted_heap_arena_size(void) {
    return sizeof(arena);
}

void hosted_heap_reset(void) {
    mapped_pages = 0;
}

uint64_t hosted_heap_mapped_pages(void) {
    return mapped_pages;
}

int heap_backend_map_page(uint64_t virtual_address) {
    uint64_t expected_address;

    expected_address = hosted_heap_arena_base() +
                       (mapped_pages * ARENA_PAGE_SIZE);

    if ((virtual_address != expected_address) ||
        ((virtual_address % ARENA_PAGE_SIZE) != 0) ||
        (mapped_pages >= ARENA_PAGE_COUNT)) {
        return 0;
    }

    mapped_pages++;
    return 1;
}
