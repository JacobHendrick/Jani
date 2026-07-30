#ifndef JANI_KERNEL_MM_LAYOUT_H
#define JANI_KERNEL_MM_LAYOUT_H

#include <stdint.h>

/*
 * THE PERMANENT ADDRESS-SPACE MAP (blueprint invariant I1)
 * =========================================================
 * One address space, forever. Every region below lives in the higher half
 * and never moves again. Phase 2 maps object versions into OBJECT SPACE;
 * the heap grows inside KERNEL HEAP; nothing is ever mapped per-process.
 *
 * The PML4 has 512 slots; each slot spans 512 GiB (0x8000000000 bytes).
 * Slots 256..511 are the higher half. Base address of slot N (N >= 256):
 *
 *     0xFFFF000000000000 + N * 0x8000000000
 *
 * The map:
 *
 *   slots 256+     HHDM           Limine's direct map of physical memory;
 *                                 base arrives at runtime in vmm_init()
 *   slots 288-415  OBJECT SPACE   0xFFFF900000000000, 64 TiB reserved for
 *                                 Phase 2 object versions
 *   slot  416      KERNEL HEAP    0xFFFFD00000000000, 512 GiB reserved,
 *                                 kmalloc's home
 *   slot  417      DEVICE MMIO    0xFFFFD08000000000, 512 GiB reserved,
 *                                 where PCI BARs are mapped uncached
 *   slot  511      KERNEL IMAGE   0xFFFFFFFF80000000 to the top of memory,
 *                                 set by linker.ld and -mcmodel=kernel
 *
 * "Reserved" costs nothing: no page tables exist until a page is mapped.
 */

#define MEMORY_LAYOUT_OBJECT_SPACE_BASE 0xFFFF900000000000ULL
#define MEMORY_LAYOUT_OBJECT_SPACE_SIZE 0x0000400000000000ULL
#define MEMORY_LAYOUT_HEAP_BASE         0xFFFFD00000000000ULL
#define MEMORY_LAYOUT_HEAP_SIZE         0x0000008000000000ULL
#define MEMORY_LAYOUT_MMIO_BASE         0xFFFFD08000000000ULL
#define MEMORY_LAYOUT_MMIO_SIZE         0x0000008000000000ULL
#define MEMORY_LAYOUT_KERNEL_IMAGE_BASE 0xFFFFFFFF80000000ULL
#define MEMORY_LAYOUT_KERNEL_IMAGE_SIZE 0x0000000080000000ULL

/* Registers the HHDM region once its base is known (called from vmm_init). */
void memory_layout_register_hhdm(uint64_t base, uint64_t size);

/* Returns a human-readable name for the region containing address, or a
 * sensible fallback for addresses outside every known region. Used by the
 * page-fault handler; must be safe to call from exception context. */
const char *memory_layout_region_name(uint64_t address);

/* Prints the full region table over serial (boot banner + demo). */
void memory_layout_print(void);

#endif
