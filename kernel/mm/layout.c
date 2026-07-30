#include <stdint.h>

#include "../lib/printk.h"
#include "layout.h"

#define MEMORY_LAYOUT_MAX_REGIONS 8

struct memory_layout_region {
    uint64_t base;
    uint64_t size;
    const char *name;
};

/* The compile-time regions. HHDM is not here: Limine chooses its base at
 * boot, so vmm_init() appends it via memory_layout_register_hhdm(). */
static struct memory_layout_region regions[MEMORY_LAYOUT_MAX_REGIONS] = {
    { MEMORY_LAYOUT_OBJECT_SPACE_BASE, MEMORY_LAYOUT_OBJECT_SPACE_SIZE,
      "object space" },
    { MEMORY_LAYOUT_HEAP_BASE, MEMORY_LAYOUT_HEAP_SIZE,
      "kernel heap" },
    { MEMORY_LAYOUT_MMIO_BASE, MEMORY_LAYOUT_MMIO_SIZE,
      "device mmio" },
    { MEMORY_LAYOUT_KERNEL_IMAGE_BASE, MEMORY_LAYOUT_KERNEL_IMAGE_SIZE,
      "kernel image" },
};

static uint64_t region_count = 4;

void memory_layout_register_hhdm(uint64_t base, uint64_t size) {
    if (region_count >= MEMORY_LAYOUT_MAX_REGIONS) {
        kputs("ERROR: memory layout region table is full\n");
        return;
    }

    regions[region_count].base = base;
    regions[region_count].size = size;
    regions[region_count].name = "hhdm";
    region_count++;
}

const char *memory_layout_region_name(uint64_t address) {
    uint64_t index;

    for (index = 0; index < region_count; index++) {
        /* Written as a subtraction because "address < base + size" wraps
         * to 0 for a region touching the top of the address space; the
         * subtraction form cannot wrap into a false negative. */
        if ((address - regions[index].base) < regions[index].size) {
            return regions[index].name;
        }
    }

    return "outside every known region";
}

void memory_layout_print(void) {
    uint64_t index;
    uint64_t last_byte;

    kputs("memory layout:\n");

    for (index = 0; index < region_count; index++) {
        /* Inclusive end: base + (size - 1) stays representable even for
         * the kernel image region, whose exclusive end is 2^64. */
        last_byte = regions[index].base + (regions[index].size - 1);
        printk("  %p .. %p  %s\n",
               (void *)(uintptr_t)regions[index].base,
               (void *)(uintptr_t)last_byte,
               regions[index].name);
    }
}
