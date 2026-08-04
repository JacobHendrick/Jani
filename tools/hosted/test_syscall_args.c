#include <stdint.h>
#include <stdio.h>

#include "../../kernel/wasm/syscall_args.h"
#include "check.h"

#define MEMORY_64K UINT64_C(65536)

unsigned long checks_passed;

static void test_span_inside_limit(void) {
    CHECK(jani_syscall_check_span(MEMORY_64K, 0, 0) == 1);
    CHECK(jani_syscall_check_span(MEMORY_64K, 0, 1) == 1);
    CHECK(jani_syscall_check_span(MEMORY_64K, 0, 65536) == 1);
    CHECK(jani_syscall_check_span(MEMORY_64K, 65535, 1) == 1);
    CHECK(jani_syscall_check_span(MEMORY_64K, 65536, 0) == 1);
}

static void test_span_outside_limit(void) {
    CHECK(jani_syscall_check_span(MEMORY_64K, 0, 65537) == 0);
    CHECK(jani_syscall_check_span(MEMORY_64K, 65535, 2) == 0);
    CHECK(jani_syscall_check_span(MEMORY_64K, 65536, 1) == 0);
    CHECK(jani_syscall_check_span(MEMORY_64K, 65537, 0) == 0);
    CHECK(jani_syscall_check_span(0, 0, 1) == 0);
}

static void test_span_rejects_wraparound(void) {
    CHECK(jani_syscall_check_span(MEMORY_64K, 0xFFFFFFFFu, 1) == 0);
    CHECK(jani_syscall_check_span(MEMORY_64K, 0xFFFFFF00u, 0x100u) == 0);
    CHECK(jani_syscall_check_span(MEMORY_64K, 1, 0xFFFFFFFFu) == 0);
    CHECK(jani_syscall_check_span(UINT64_MAX, 0xFFFFFFFFu, 0xFFFFFFFFu) == 1);
}

static void test_slot_bounds(void) {
    CHECK(jani_syscall_check_slot(4, 0) == 1);
    CHECK(jani_syscall_check_slot(4, 3) == 1);
    CHECK(jani_syscall_check_slot(4, 4) == 0);
    CHECK(jani_syscall_check_slot(4, -1) == 0);
    CHECK(jani_syscall_check_slot(4, INT32_MIN) == 0);
    CHECK(jani_syscall_check_slot(0, 0) == 0);
}

static void test_optional_slot_accepts_none(void) {
    CHECK(jani_syscall_check_optional_slot(4, -1) == 1);
    CHECK(jani_syscall_check_optional_slot(0, -1) == 1);
    CHECK(jani_syscall_check_optional_slot(4, 2) == 1);
    CHECK(jani_syscall_check_optional_slot(4, 9) == 0);
    CHECK(jani_syscall_check_optional_slot(4, -2) == 0);
}

static void test_clamp_read(void) {
    uint32_t length;

    length = 0xFFFFFFFFu;
    CHECK(jani_syscall_clamp_read(100, 0, 10, &length) == 1);
    CHECK(length == 10);

    length = 0xFFFFFFFFu;
    CHECK(jani_syscall_clamp_read(100, 95, 10, &length) == 1);
    CHECK(length == 5);

    length = 0xFFFFFFFFu;
    CHECK(jani_syscall_clamp_read(100, 100, 10, &length) == 1);
    CHECK(length == 0);

    length = 0xFFFFFFFFu;
    CHECK(jani_syscall_clamp_read(100, 101, 10, &length) == 0);
    CHECK(length == 0);

    length = 0xFFFFFFFFu;
    CHECK(jani_syscall_clamp_read(100, 0, 0xFFFFFFFFu, &length) == 1);
    CHECK(length == 100);

    CHECK(jani_syscall_clamp_read(100, 0, 10, NULL) == 0);
}

static void test_transfer_checks_both_spans(void) {
    CHECK(jani_syscall_check_transfer(MEMORY_64K, 0, 100, 0, 10) == 1);
    CHECK(jani_syscall_check_transfer(MEMORY_64K, 65530, 100, 0, 10) == 0);
    CHECK(jani_syscall_check_transfer(MEMORY_64K, 0, 100, 95, 10) == 0);
    CHECK(jani_syscall_check_transfer(MEMORY_64K, 0, 100, 100, 0) == 1);
    CHECK(jani_syscall_check_transfer(MEMORY_64K, 0xFFFFFFFFu, 100, 0, 1) == 0);
}

int main(void) {
    checks_passed = 0;

    test_span_inside_limit();
    test_span_outside_limit();
    test_span_rejects_wraparound();
    test_slot_bounds();
    test_optional_slot_accepts_none();
    test_clamp_read();
    test_transfer_checks_both_spans();

    printf("test_syscall_args: %lu checks passed\n", checks_passed);
    return 0;
}
