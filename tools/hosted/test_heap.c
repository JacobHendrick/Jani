/* Hosted tests for the real heap allocator in kernel/mm/heap.c. */
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/mm/heap.h"
#include "check.h"
#include "hosted_heap.h"

unsigned long checks_passed;

static void boot_heap(uint64_t size) {
    hosted_heap_reset();
    kheap_init(hosted_heap_arena_base(), size);
}

static void test_invalid_initialization(void) {
    uint64_t base = hosted_heap_arena_base();

    kheap_init(0, 4096);
    CHECK(kmalloc(16) == NULL);

    kheap_init(base + 1, 4096);
    CHECK(kmalloc(16) == NULL);

    kheap_init(base, 0);
    CHECK(kmalloc(16) == NULL);

    kheap_init(UINT64_MAX - 4095, 8192);
    CHECK(kmalloc(16) == NULL);
}

static void test_alignment_and_accounting(void) {
    uint8_t *first;
    uint8_t *second;

    boot_heap(hosted_heap_arena_size());
    CHECK(kmalloc(0) == NULL);

    first = kmalloc(1);
    second = kmalloc(17);

    CHECK(first != NULL);
    CHECK(second != NULL);
    CHECK(((uintptr_t)first % 16) == 0);
    CHECK(((uintptr_t)second % 16) == 0);
    CHECK(second > first);
    CHECK(kheap_used_bytes() == 80);
    CHECK(hosted_heap_mapped_pages() == 1);

    first[0] = 0xA5;
    second[0] = 0x5A;
    second[16] = 0xC3;
    CHECK(first[0] == 0xA5);
    CHECK(second[0] == 0x5A);
    CHECK(second[16] == 0xC3);
}

static void test_crosses_page_boundary(void) {
    uint8_t *first;
    uint8_t *second;

    boot_heap(hosted_heap_arena_size());
    first = kmalloc(4000);
    second = kmalloc(200);

    CHECK(first != NULL);
    CHECK(second != NULL);
    CHECK(hosted_heap_mapped_pages() == 2);

    first[3999] = 0x11;
    second[199] = 0x22;
    CHECK(first[3999] == 0x11);
    CHECK(second[199] == 0x22);
}

static void test_heap_limit(void) {
    void *whole_heap;

    boot_heap(8192);
    whole_heap = kmalloc(8176);

    CHECK(whole_heap != NULL);
    CHECK(hosted_heap_mapped_pages() == 2);
    CHECK(kmalloc(1) == NULL);
    CHECK(kheap_used_bytes() == 8192);
}

static void test_freed_block_is_reused(void) {
    void *first;
    void *second;
    void *reused;

    boot_heap(hosted_heap_arena_size());
    first = kmalloc(64);
    second = kmalloc(64);

    CHECK(first != NULL);
    CHECK(second != NULL);
    CHECK(kheap_used_bytes() == 160);

    kfree(first);
    kfree(first);
    reused = kmalloc(32);

    CHECK(reused == first);
    CHECK(kheap_used_bytes() == 160);
    CHECK(hosted_heap_mapped_pages() == 1);

    kfree(NULL);
}

static void test_size_classes_prefer_smaller_block(void) {
    void *small;
    void *large;
    void *guard;
    void *reused;

    boot_heap(hosted_heap_arena_size());
    small = kmalloc(32);
    large = kmalloc(200);
    guard = kmalloc(16);

    CHECK(small != NULL);
    CHECK(large != NULL);
    CHECK(guard != NULL);

    kfree(small);
    kfree(large);
    reused = kmalloc(16);

    CHECK(reused == small);
    CHECK(kheap_used_bytes() == 304);
}

static void test_realloc_null_and_zero(void) {
    void *allocated;
    void *reused;

    boot_heap(hosted_heap_arena_size());
    allocated = krealloc(NULL, 32);

    CHECK(allocated != NULL);
    CHECK(krealloc(allocated, 0) == NULL);

    reused = kmalloc(32);
    CHECK(reused == allocated);
}

static void test_realloc_grows_and_preserves_data(void) {
    uint8_t *original;
    uint8_t *grown;
    size_t index;

    boot_heap(hosted_heap_arena_size());
    original = kmalloc(16);
    CHECK(original != NULL);

    for (index = 0; index < 16; index++) {
        original[index] = (uint8_t)(index + 1);
    }

    grown = krealloc(original, 128);
    CHECK(grown != NULL);
    CHECK(grown != original);

    for (index = 0; index < 16; index++) {
        CHECK(grown[index] == (uint8_t)(index + 1));
    }
}

static void test_realloc_shrinks_in_place(void) {
    uint8_t *original;
    uint8_t *shrunk;

    boot_heap(hosted_heap_arena_size());
    original = kmalloc(128);
    CHECK(original != NULL);
    original[0] = 0xA5;

    shrunk = krealloc(original, 32);

    CHECK(shrunk == original);
    CHECK(shrunk[0] == 0xA5);
}

static void test_realloc_failure_keeps_original(void) {
    uint8_t *original;

    boot_heap(4096);
    original = kmalloc(4000);
    CHECK(original != NULL);
    original[0] = 0x5A;

    CHECK(krealloc(original, 5000) == NULL);
    CHECK(original[0] == 0x5A);
}

int main(void) {
    test_invalid_initialization();
    test_alignment_and_accounting();
    test_crosses_page_boundary();
    test_heap_limit();
    test_freed_block_is_reused();
    test_size_classes_prefer_smaller_block();
    test_realloc_null_and_zero();
    test_realloc_grows_and_preserves_data();
    test_realloc_shrinks_in_place();
    test_realloc_failure_keeps_original();

    printf("test_heap: %lu checks passed\n", checks_passed);
    return 0;
}
