#include <stdint.h>

#include "../lib/printk.h"
#include "../lib/string.h"
#include "memory_map.h"
#include "pmm.h"

#define BITS_PER_BYTE 8ULL
#define PMM_BITMAP_SIZE (PMM_MAX_FRAMES / BITS_PER_BYTE)

static uint8_t frame_bitmap[PMM_BITMAP_SIZE];
static uint64_t free_frame_count;
static uint64_t next_search_frame;

static uint64_t byte_for_frame(uint64_t frame_number) {
    return frame_number / BITS_PER_BYTE;
}

static uint8_t mask_for_frame(uint64_t frame_number) {
    return (uint8_t)(1u << (frame_number % BITS_PER_BYTE));
}

int pmm_frame_is_used(uint64_t frame_number) {
    if (frame_number >= PMM_MAX_FRAMES) {
        return 1;
    }

    return (frame_bitmap[byte_for_frame(frame_number)] &
            mask_for_frame(frame_number)) != 0;
}

static void pmm_bitmap_reset(void) {
    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    free_frame_count = 0;
    next_search_frame = 1;
}

static void pmm_mark_frame_used(uint64_t frame_number) {
    if ((frame_number >= PMM_MAX_FRAMES) ||
        pmm_frame_is_used(frame_number)) {
        return;
    }

    frame_bitmap[byte_for_frame(frame_number)] |=
        mask_for_frame(frame_number);
    free_frame_count--;
}

static void pmm_mark_frame_free(uint64_t frame_number) {
    if ((frame_number >= PMM_MAX_FRAMES) ||
        !pmm_frame_is_used(frame_number)) {
        return;
    }

    frame_bitmap[byte_for_frame(frame_number)] &=
        (uint8_t)~mask_for_frame(frame_number);
    free_frame_count++;
}

static void pmm_release_usable_region(const struct memory_map_entry *entry) {
    uint64_t region_end;
    uint64_t first_frame;
    uint64_t end_frame;
    uint64_t frame;

    if (entry->base >= PMM_MAX_PHYSICAL_MEMORY) {
        return;
    }

    if (entry->length > (PMM_MAX_PHYSICAL_MEMORY - entry->base)) {
        region_end = PMM_MAX_PHYSICAL_MEMORY;
    } else {
        region_end = entry->base + entry->length;
    }

    first_frame = (entry->base + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    end_frame = region_end / PMM_FRAME_SIZE;

    for (frame = first_frame; frame < end_frame; frame++) {
        pmm_mark_frame_free(frame);
    }
}

void pmm_init(void) {
    struct memory_map_entry entry;
    uint64_t index;

    pmm_bitmap_reset();

    for (index = 0; index < memory_map_entry_count(); index++) {
        if (memory_map_get_entry(index, &entry) &&
            (entry.type == MEMORY_MAP_USABLE)) {
            pmm_release_usable_region(&entry);
        }
    }

    pmm_mark_frame_used(0);
    printk("pmm init: %d free frames\n", (int)free_frame_count);
}

static uint64_t pmm_allocate_between(uint64_t first, uint64_t end) {
    uint64_t frame;

    for (frame = first; frame < end; frame++) {
        if (!pmm_frame_is_used(frame)) {
            pmm_mark_frame_used(frame);
            next_search_frame = frame + 1;

            if (next_search_frame >= PMM_MAX_FRAMES) {
                next_search_frame = 1;
            }

            return frame * PMM_FRAME_SIZE;
        }
    }

    return 0;
}

uint64_t pmm_alloc_frame(void) {
    uint64_t physical_address;

    physical_address = pmm_allocate_between(next_search_frame,
                                            PMM_MAX_FRAMES);
    if (physical_address != 0) {
        return physical_address;
    }

    return pmm_allocate_between(1, next_search_frame);
}

void pmm_free_frame(uint64_t physical_address) {
    uint64_t frame;

    if ((physical_address == 0) ||
        ((physical_address % PMM_FRAME_SIZE) != 0)) {
        return;
    }

    frame = physical_address / PMM_FRAME_SIZE;

    if ((frame >= PMM_MAX_FRAMES) || !pmm_frame_is_used(frame)) {
        return;
    }

    pmm_mark_frame_free(frame);

    if (frame < next_search_frame) {
        next_search_frame = frame;
    }
}

uint64_t pmm_get_free_frame_count(void) {
    return free_frame_count;
}
