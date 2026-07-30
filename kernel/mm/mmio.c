#include <stdint.h>

#include "layout.h"
#include "mmio.h"
#include "vmm.h"

#define MMIO_PAGE_SIZE 4096ULL

static uint64_t mmio_next_virtual = MEMORY_LAYOUT_MMIO_BASE;

void *mmio_map(uint64_t physical_address, uint64_t length) {
    uint64_t page_offset;
    uint64_t first_physical;
    uint64_t page_count;
    uint64_t span;
    uint64_t virtual_base;
    uint64_t index;

    if (length == 0) {
        return 0;
    }

    page_offset = physical_address & (MMIO_PAGE_SIZE - 1ULL);
    first_physical = physical_address - page_offset;

    if (length > (UINT64_MAX - page_offset - (MMIO_PAGE_SIZE - 1ULL))) {
        return 0;
    }
    page_count = (page_offset + length + (MMIO_PAGE_SIZE - 1ULL)) /
                 MMIO_PAGE_SIZE;
    span = page_count * MMIO_PAGE_SIZE;

    if (span > MEMORY_LAYOUT_MMIO_SIZE) {
        return 0;
    }
    if ((mmio_next_virtual - MEMORY_LAYOUT_MMIO_BASE) >
        (MEMORY_LAYOUT_MMIO_SIZE - span)) {
        return 0;
    }

    virtual_base = mmio_next_virtual;
    for (index = 0; index < page_count; index++) {
        if (!vmm_map_page(virtual_base + (index * MMIO_PAGE_SIZE),
                          first_physical + (index * MMIO_PAGE_SIZE),
                          VMM_PAGE_WRITABLE | VMM_PAGE_CACHE_DISABLE |
                          VMM_PAGE_NO_EXECUTE)) {
            while (index > 0) {
                index--;
                (void)vmm_unmap_page(virtual_base + (index * MMIO_PAGE_SIZE));
            }
            return 0;
        }
    }

    mmio_next_virtual = virtual_base + span;
    return (void *)(uintptr_t)(virtual_base + page_offset);
}
