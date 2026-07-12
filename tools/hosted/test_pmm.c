/* Hosted twin tests for the PMM bitmap allocator (blueprint rule R3).
 *
 * This compiles the REAL kernel/mm/pmm.c - byte-for-byte the kernel's
 * allocator - against a scripted memory map, as an ordinary Linux binary
 * under ASan + UBSan.
 *
 * Build + run:  make test
 */
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/mm/memory_map.h"
#include "../../kernel/mm/pmm.h"
#include "check.h"
#include "fake_memory_map.h"

unsigned long checks_passed;

/* A convenient little machine: one usable region of `frames` 4 KiB
 * frames starting at 1 MiB. pmm_init() may be called repeatedly - each
 * call re-reads the current fake map from scratch. */
static void boot_simple_machine(uint64_t frames) {
    fake_memory_map_reset();
    fake_memory_map_add(0x100000, frames * PMM_FRAME_SIZE,
                        MEMORY_MAP_USABLE);
    pmm_init();
}

/*
 * TODO(jacob) test 1 - init counts what the map offers.
 *   boot_simple_machine(64): what must pmm_get_free_frame_count() say?
 *
 * TODO(jacob) test 2 - allocation basics.
 *   Every pmm_alloc_frame() result should be nonzero, 4 KiB aligned,
 *   inside the scripted region, and pmm_frame_is_used() afterward.
 *   Two consecutive allocations must return different frames.
 *
 * TODO(jacob) test 3 - free really frees.
 *   alloc -> free -> the free count returns to its starting value, and
 *   the same frame can be allocated again.
 *
 * TODO(jacob) test 4 - exhaustion.
 *   Drain a small machine dry: exactly the expected number of frames
 *   come out, then pmm_alloc_frame() returns 0. Free one frame and the
 *   next allocation must succeed again.
 *
 * TODO(jacob) test 5 - hostile frees are refused.
 *   pmm_free_frame(0), an unaligned address, a never-allocated address:
 *   none of them may disturb the free count. (R1 thinking: invalid
 *   input is refused, not absorbed.)
 *
 * TODO(jacob) test 6 - the map is clipped and aligned.
 *   A usable region straddling PMM_MAX_PHYSICAL_MEMORY contributes only
 *   the frames below the cap. A region whose base is NOT frame-aligned
 *   must start at the next whole frame, not the partial one.
 */

int main(void) {
    /* Proof of life for the harness; replace with calls to your tests. */
    boot_simple_machine(64);

    printf("test_pmm: %lu checks passed\n", checks_passed);
    return 0;
}
