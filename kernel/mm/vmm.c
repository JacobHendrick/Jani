#include <stdint.h>

#include "../lib/printk.h"
#include "../lib/string.h"
#include "layout.h"
#include "pmm.h"
#include "vmm.h"

#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b


#define LIMINE_HHDM_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }

#define PAGE_TABLE_ENTRY_COUNT 512
#define PAGE_TABLE_INDEX_MASK 0x1FFULL
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define PAGE_OFFSET_MASK 0xFFFULL
#define PAGE_ENTRY_HUGE (1ULL << 7)

/* The self-test exercises the first page of object space: map, write,
 * translate, unmap - the exact life cycle Phase 2 gives object versions. */
#define VMM_TEST_VIRTUAL_ADDRESS MEMORY_LAYOUT_OBJECT_SPACE_BASE
#define VMM_TEST_VALUE 0x4A414E49ULL

struct page_table {
    uint64_t entries[PAGE_TABLE_ENTRY_COUNT];
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = 0
};

static uint64_t hhdm_offset;
static uint64_t root_page_table_physical;
static struct page_table *root_page_table;
static int vmm_ready;

static uint16_t pml4_index(uint64_t address) {
    return (uint16_t)((address >> 39) & PAGE_TABLE_INDEX_MASK);
}

static uint16_t pdpt_index(uint64_t address) {
    return (uint16_t)((address >> 30) & PAGE_TABLE_INDEX_MASK);
}

static uint16_t pd_index(uint64_t address) {
    return (uint16_t)((address >> 21) & PAGE_TABLE_INDEX_MASK);
}

static uint16_t pt_index(uint64_t address) {
    return (uint16_t)((address >> 12) & PAGE_TABLE_INDEX_MASK);
}

static int address_is_canonical(uint64_t address) {
    uint64_t upper_bits = address >> 48;
    uint64_t sign_bit = (address >> 47) & 1ULL;

    if (sign_bit != 0) {
        return upper_bits == 0xFFFFULL;
    }

    return upper_bits == 0;
}

static uint64_t read_cr3(void) {
    uint64_t value;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));

    return value;
}

void vmm_init(void) {
    struct limine_hhdm_response *response;

    response = hhdm_request.response;

    if (response == 0) {
        kputs("ERROR: Limine did not provide the HHDM\n");
        return;
    }

    hhdm_offset = response->offset;
    root_page_table_physical = read_cr3() & PAGE_ADDRESS_MASK;
    root_page_table = (struct page_table *)(uintptr_t)
        (hhdm_offset + root_page_table_physical);
    vmm_ready = 1;

    /* The PMM only manages the first 4 GiB for now, so that is the span
     * of physical memory the kernel actually reaches through the HHDM. */
    memory_layout_register_hhdm(hhdm_offset, PMM_MAX_PHYSICAL_MEMORY);

    printk("vmm hhdm offset: %p\n", (void *)(uintptr_t)hhdm_offset);
    printk("vmm root table: %p\n",
           (void *)(uintptr_t)root_page_table_physical);
}

int vmm_is_ready(void) {
    return vmm_ready;
}

uint64_t vmm_get_hhdm_offset(void) {
    return hhdm_offset;
}

void *vmm_physical_to_virtual(uint64_t physical_address) {
    if (!vmm_ready) {
        return 0;
    }

    return (void *)(uintptr_t)(hhdm_offset + physical_address);
}

void vmm_print_indices(uint64_t virtual_address) {
    printk("vmm indices: %d %d %d %d\n",
           (int)pml4_index(virtual_address),
           (int)pdpt_index(virtual_address),
           (int)pd_index(virtual_address),
           (int)pt_index(virtual_address));
}

static struct page_table *table_from_entry(uint64_t entry) {
    uint64_t physical_address;

    if (((entry & VMM_PAGE_PRESENT) == 0) ||
        ((entry & PAGE_ENTRY_HUGE) != 0)) {
        return 0;
    }

    physical_address = entry & PAGE_ADDRESS_MASK;
    return (struct page_table *)vmm_physical_to_virtual(physical_address);
}

static struct page_table *get_or_create_table(uint64_t *entry,
                                               uint64_t flags) {
    struct page_table *table;
    uint64_t physical_address;
    uint64_t table_flags;

    table_flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE;
    if ((flags & VMM_PAGE_USER) != 0) {
        table_flags |= VMM_PAGE_USER;
    }

    if ((*entry & VMM_PAGE_PRESENT) != 0) {
        if ((*entry & PAGE_ENTRY_HUGE) != 0) {
            return 0;
        }

        *entry |= table_flags;
        return table_from_entry(*entry);
    }

    physical_address = pmm_alloc_frame();
    if (physical_address == 0) {
        return 0;
    }

    table = (struct page_table *)vmm_physical_to_virtual(physical_address);
    if (table == 0) {
        pmm_free_frame(physical_address);
        return 0;
    }

    memset(table, 0, PMM_FRAME_SIZE);
    *entry = physical_address | table_flags;
    return table;
}

static uint64_t *find_leaf_entry(uint64_t virtual_address, int create,
                                 uint64_t flags) {
    struct page_table *pdpt;
    struct page_table *pd;
    struct page_table *pt;
    uint64_t *entry;

    if (!vmm_ready || !address_is_canonical(virtual_address)) {
        return 0;
    }

    entry = &root_page_table->entries[pml4_index(virtual_address)];
    pdpt = create ? get_or_create_table(entry, flags)
                  : table_from_entry(*entry);
    if (pdpt == 0) {
        return 0;
    }

    entry = &pdpt->entries[pdpt_index(virtual_address)];
    pd = create ? get_or_create_table(entry, flags)
                : table_from_entry(*entry);
    if (pd == 0) {
        return 0;
    }

    entry = &pd->entries[pd_index(virtual_address)];
    pt = create ? get_or_create_table(entry, flags)
                : table_from_entry(*entry);
    if (pt == 0) {
        return 0;
    }

    return &pt->entries[pt_index(virtual_address)];
}

static void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

int vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                 uint64_t flags) {
    uint64_t *leaf_entry;

    if (((virtual_address & PAGE_OFFSET_MASK) != 0) ||
        ((physical_address & PAGE_OFFSET_MASK) != 0) ||
        ((physical_address & ~PAGE_ADDRESS_MASK) != 0) ||
        (((flags & VMM_PAGE_WRITABLE) != 0) &&
         ((flags & VMM_PAGE_NO_EXECUTE) == 0))) {
        return 0;
    }

    leaf_entry = find_leaf_entry(virtual_address, 1, flags);
    if ((leaf_entry == 0) || ((*leaf_entry & VMM_PAGE_PRESENT) != 0)) {
        return 0;
    }

    *leaf_entry = physical_address | VMM_PAGE_PRESENT |
                  (flags & (VMM_PAGE_WRITABLE |
                            VMM_PAGE_USER |
                            VMM_PAGE_WRITE_THROUGH |
                            VMM_PAGE_CACHE_DISABLE |
                            VMM_PAGE_NO_EXECUTE));
    invalidate_page(virtual_address);
    return 1;
}

uint64_t vmm_virtual_to_physical(uint64_t virtual_address) {
    uint64_t *leaf_entry;

    leaf_entry = find_leaf_entry(virtual_address, 0, 0);
    if ((leaf_entry == 0) || ((*leaf_entry & VMM_PAGE_PRESENT) == 0)) {
        return 0;
    }

    return (*leaf_entry & PAGE_ADDRESS_MASK) |
           (virtual_address & PAGE_OFFSET_MASK);
}

uint64_t vmm_unmap_page(uint64_t virtual_address) {
    uint64_t *leaf_entry;
    uint64_t physical_address;

    if ((virtual_address & PAGE_OFFSET_MASK) != 0) {
        return 0;
    }

    leaf_entry = find_leaf_entry(virtual_address, 0, 0);
    if ((leaf_entry == 0) || ((*leaf_entry & VMM_PAGE_PRESENT) == 0)) {
        return 0;
    }

    physical_address = *leaf_entry & PAGE_ADDRESS_MASK;
    *leaf_entry = 0;
    invalidate_page(virtual_address);
    return physical_address;
}

int vmm_self_test(void) {
    volatile uint64_t *test_pointer;
    uint64_t physical_address;
    uint64_t translated_address;
    uint64_t unmapped_address;

    vmm_print_indices(VMM_TEST_VIRTUAL_ADDRESS);

    physical_address = pmm_alloc_frame();
    if (physical_address == 0) {
        return 0;
    }

    if (vmm_map_page(VMM_TEST_VIRTUAL_ADDRESS, physical_address,
                     VMM_PAGE_WRITABLE)) {
        unmapped_address = vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        if (unmapped_address != 0) {
            pmm_free_frame(unmapped_address);
        }
        return 0;
    }

    if (!vmm_map_page(VMM_TEST_VIRTUAL_ADDRESS, physical_address,
                      VMM_PAGE_WRITABLE | VMM_PAGE_NO_EXECUTE)) {
        pmm_free_frame(physical_address);
        return 0;
    }

    test_pointer = (volatile uint64_t *)(uintptr_t)VMM_TEST_VIRTUAL_ADDRESS;
    *test_pointer = VMM_TEST_VALUE;

    translated_address =
        vmm_virtual_to_physical(VMM_TEST_VIRTUAL_ADDRESS);
    if ((*test_pointer != VMM_TEST_VALUE) ||
        (translated_address != physical_address)) {
        unmapped_address = vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        if (unmapped_address != 0) {
            pmm_free_frame(unmapped_address);
        }
        return 0;
    }

    unmapped_address = vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
    if (unmapped_address != physical_address) {
        return 0;
    }

    pmm_free_frame(physical_address);
    printk("vmm test frame: %p\n", (void *)(uintptr_t)physical_address);
    return 1;
}
