#ifndef JANI_KERNEL_MM_HEAP_H
#define JANI_KERNEL_MM_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* The page-source seam: whoever hosts the heap must back one 4 KiB page
 * at this (page-aligned) virtual address. Kernel: PMM frame + VMM map.
 * Hosted tests: a static arena. Returns 1 on success, 0 on failure. */
int heap_backend_map_page(uint64_t virtual_address);

/* Adopts [base, base + size) as the heap's territory. Call once at boot
 * with the layout.h heap region. base must be page-aligned. */
void kheap_init(uint64_t base, uint64_t size);

/* Returns a 16-byte-aligned block of at least size bytes, or a null
 * pointer if the heap cannot satisfy it. Freed blocks may be reused. */
void *kmalloc(size_t size);

/* Returns an allocation to the heap so kmalloc can reuse it. */
void kfree(void *memory);

/* Resizes an allocation while preserving its existing contents. */
void *krealloc(void *memory, size_t new_size);

/* Bytes consumed so far - for tests now, U10 metering later. */
uint64_t kheap_used_bytes(void);

#endif
