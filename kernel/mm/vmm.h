#ifndef JANI_KERNEL_MM_VMM_H
#define JANI_KERNEL_MM_VMM_H

#include <stdint.h>

#define VMM_PAGE_PRESENT (1ULL << 0)
#define VMM_PAGE_WRITABLE (1ULL << 1)
#define VMM_PAGE_USER (1ULL << 2)
#define VMM_PAGE_NO_EXECUTE (1ULL << 63)

void vmm_init(void);
int vmm_is_ready(void);
uint64_t vmm_get_hhdm_offset(void);
void *vmm_physical_to_virtual(uint64_t physical_address);
void vmm_print_indices(uint64_t virtual_address);
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                 uint64_t flags);
uint64_t vmm_virtual_to_physical(uint64_t virtual_address);
uint64_t vmm_unmap_page(uint64_t virtual_address);
int vmm_self_test(void);

#endif
