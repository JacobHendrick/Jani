#include <stddef.h>
#include <stdint.h>

#include "../lib/string.h"
#include "free_list.h"
#include "heap.h"

#define HEAP_ALIGNMENT 16ULL
#define HEAP_PAGE_SIZE 4096ULL
#define HEAP_ALLOCATION_MAGIC UINT64_C(0x4A414E4948454150)

struct allocation_header {
    size_t block_size;
    uint64_t magic;
};

static uint64_t heap_base;
static uint64_t heap_limit;
static uint64_t heap_next;
static uint64_t heap_mapped_end;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

static int calculate_block_size(size_t requested_size, size_t *block_size) {
    size_t size_with_header;

    if (requested_size > (SIZE_MAX - sizeof(struct allocation_header))) {
        return 0;
    }

    size_with_header = requested_size + sizeof(struct allocation_header);

    if (size_with_header > (SIZE_MAX - (HEAP_ALIGNMENT - 1))) {
        return 0;
    }

    *block_size = (size_t)align_up(size_with_header, HEAP_ALIGNMENT);
    return 1;
}

void kheap_init(uint64_t base, uint64_t size) {
    heap_base = 0;
    heap_limit = 0;
    heap_next = 0;
    heap_mapped_end = 0;
    heap_free_list_reset();

    if ((base == 0) || (size == 0) ||
        ((base % HEAP_PAGE_SIZE) != 0) ||
        (size > (UINT64_MAX - base))) {
        return;
    }

    heap_base = base;
    heap_limit = base + size;
    heap_next = base;
    heap_mapped_end = base;
}

void *kmalloc(size_t size) {
    struct allocation_header *header;
    size_t block_size;
    size_t reused_size;
    uint64_t block_start;
    uint64_t new_next;

    if ((size == 0) || (heap_base == 0)) {
        return NULL;
    }

    if (!calculate_block_size(size, &block_size)) {
        return NULL;
    }

    header = heap_free_list_take(block_size, &reused_size);

    if (header != NULL) {
        header->block_size = reused_size;
        header->magic = HEAP_ALLOCATION_MAGIC;
        return header + 1;
    }

    block_start = align_up(heap_next, HEAP_ALIGNMENT);

    if ((block_start > heap_limit) ||
        (block_size > (heap_limit - block_start))) {
        return NULL;
    }

    new_next = block_start + block_size;

    while (heap_mapped_end < new_next) {
        if (!heap_backend_map_page(heap_mapped_end)) {
            return NULL;
        }

        heap_mapped_end += HEAP_PAGE_SIZE;
    }

    header = (struct allocation_header *)(uintptr_t)block_start;
    header->block_size = block_size;
    header->magic = HEAP_ALLOCATION_MAGIC;

    heap_next = new_next;
    return header + 1;
}

void kfree(void *memory) {
    struct allocation_header *header;
    size_t block_size;

    if (memory == NULL) {
        return;
    }

    header = ((struct allocation_header *)memory) - 1;

    if (header->magic != HEAP_ALLOCATION_MAGIC) {
        return;
    }

    block_size = header->block_size;
    header->magic = 0;

    (void)heap_free_list_add(header, block_size);
}

void *krealloc(void *memory, size_t new_size) {
    struct allocation_header *header;
    size_t old_capacity;
    void *new_memory;

    if (memory == NULL) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(memory);
        return NULL;
    }

    header = ((struct allocation_header *)memory) - 1;

    if (header->magic != HEAP_ALLOCATION_MAGIC) {
        return NULL;
    }

    if (header->block_size < sizeof(struct allocation_header)) {
        return NULL;
    }

    old_capacity =
        header->block_size - sizeof(struct allocation_header);

    if (new_size <= old_capacity) {
        return memory;
    }

    new_memory = kmalloc(new_size);

    if (new_memory == NULL) {
        return NULL;
    }

    memcpy(new_memory, memory, old_capacity);
    kfree(memory);

    return new_memory;
}
uint64_t kheap_used_bytes(void) {
    return heap_next - heap_base;
}
