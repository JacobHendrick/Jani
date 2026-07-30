#include <stdint.h>

#include "heap.h"
#include "pmm.h"
#include "vmm.h"

int heap_backend_map_page(uint64_t virtual_address) {
    uint64_t frame;

    frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }

    if (!vmm_map_page(virtual_address, frame,
                      VMM_PAGE_WRITABLE | VMM_PAGE_NO_EXECUTE)) {
        pmm_free_frame(frame);
        return 0;
    }

    return 1;
}
