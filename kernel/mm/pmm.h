#ifndef JANI_KERNEL_MM_PMM_H
#define JANI_KERNEL_MM_PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE 4096ULL
#define PMM_MAX_PHYSICAL_MEMORY 0x100000000ULL
#define PMM_MAX_FRAMES (PMM_MAX_PHYSICAL_MEMORY / PMM_FRAME_SIZE)

void pmm_init(void);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t physical_address);
uint64_t pmm_get_free_frame_count(void);
int pmm_frame_is_used(uint64_t frame_number);

#endif
