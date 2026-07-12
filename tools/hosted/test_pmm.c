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

static void test_init_counts_the_map(void) {
    boot_simple_machine(64);
    CHECK(pmm_get_free_frame_count() == 64);

    boot_simple_machine(1);
    CHECK(pmm_get_free_frame_count() == 1);
}

static void test_allocation_basics(void) {
    uint64_t first;
    uint64_t second;

    boot_simple_machine(64);

    first = pmm_alloc_frame();
    CHECK(first != 0);
    CHECK((first % PMM_FRAME_SIZE) == 0);
    CHECK(first >= 0x100000);
    CHECK(first < 0x100000 + (64 * PMM_FRAME_SIZE));
    CHECK(pmm_frame_is_used(first / PMM_FRAME_SIZE));

    second = pmm_alloc_frame();
    CHECK(second != 0);
    CHECK(second != first);
    CHECK(pmm_get_free_frame_count() == 62);
}

static void test_free_really_frees(void) {
    uint64_t allocated;
    uint64_t count_before;

    boot_simple_machine(8);
    count_before = pmm_get_free_frame_count();

    allocated = pmm_alloc_frame();
    CHECK(pmm_get_free_frame_count() == count_before - 1);

    pmm_free_frame(allocated);
    CHECK(pmm_get_free_frame_count() == count_before);
    CHECK(!pmm_frame_is_used(allocated / PMM_FRAME_SIZE));
    CHECK(pmm_alloc_frame() == allocated);
}

static void test_exhaustion(void) {
    uint64_t frames[8];
    uint64_t index;

    boot_simple_machine(8);

    for (index = 0; index < 8; index++) {
        frames[index] = pmm_alloc_frame();
        CHECK(frames[index] != 0);
    }

    CHECK(pmm_get_free_frame_count() == 0);
    CHECK(pmm_alloc_frame() == 0);

    pmm_free_frame(frames[3]);
    CHECK(pmm_alloc_frame() == frames[3]);
}

static void test_hostile_frees_are_refused(void) {
    uint64_t allocated;
    uint64_t count_before;

    boot_simple_machine(8);
    allocated = pmm_alloc_frame();
    CHECK(allocated != 0);
    count_before = pmm_get_free_frame_count();

    pmm_free_frame(0);
    pmm_free_frame(allocated + 123);
    pmm_free_frame(PMM_MAX_PHYSICAL_MEMORY);
    CHECK(pmm_get_free_frame_count() == count_before);

    pmm_free_frame(allocated);
    CHECK(pmm_get_free_frame_count() == count_before + 1);
    pmm_free_frame(allocated);
    CHECK(pmm_get_free_frame_count() == count_before + 1);
}

static void test_map_clipping_and_alignment(void) {
    fake_memory_map_reset();
    fake_memory_map_add(PMM_MAX_PHYSICAL_MEMORY - (4 * PMM_FRAME_SIZE),
                        8 * PMM_FRAME_SIZE, MEMORY_MAP_USABLE);
    pmm_init();
    CHECK(pmm_get_free_frame_count() == 4);

    fake_memory_map_reset();
    fake_memory_map_add(PMM_MAX_PHYSICAL_MEMORY, 8 * PMM_FRAME_SIZE,
                        MEMORY_MAP_USABLE);
    pmm_init();
    CHECK(pmm_get_free_frame_count() == 0);

    fake_memory_map_reset();
    fake_memory_map_add(0x100000 + 123, 2 * PMM_FRAME_SIZE,
                        MEMORY_MAP_USABLE);
    pmm_init();
    CHECK(pmm_get_free_frame_count() == 1);
}

int main(void) {
    test_init_counts_the_map();
    test_allocation_basics();
    test_free_really_frees();
    test_exhaustion();
    test_hostile_frees_are_refused();
    test_map_clipping_and_alignment();

    printf("test_pmm: %lu checks passed\n", checks_passed);
    return 0;
}
