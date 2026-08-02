#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/wasm/module.h"
#include "check.h"

unsigned long checks_passed;

#define WASM_HEADER 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00

static void test_rejects_short_input(void) {
    const uint8_t bytes[] = { WASM_HEADER };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(0, 0, &sections));
    CHECK(!jani_wasm_module_validate(bytes, 0, &sections));
    CHECK(!jani_wasm_module_validate(bytes, 7, &sections));
}

static void test_rejects_bad_magic(void) {
    const uint8_t bytes[] = { 'X', 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_bad_version(void) {
    const uint8_t bytes[] = {
        0x00, 0x61, 0x73, 0x6d, 0x02, 0x00, 0x00, 0x00
    };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_accepts_header_only_module(void) {
    const uint8_t bytes[] = { WASM_HEADER };
    uint32_t sections;

    sections = 0xFFFFFFFFu;
    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 0);
}

static void test_accepts_one_empty_section(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x01, 0x00 };
    uint32_t sections;

    sections = 0;
    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 1);
}

static void test_accepts_section_with_payload(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x03, 0x02, 0xAA, 0xBB };
    uint32_t sections;

    sections = 0;
    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 1);
}

static void test_rejects_truncated_section_payload(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x01, 0x7F };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_missing_section_length(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x01 };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_unknown_section_id(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x7E, 0x00 };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_duplicate_non_custom_section(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x01, 0x00, 0x01, 0x00 };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_allows_repeated_custom_sections(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x00, 0x00, 0x00, 0x00 };
    uint32_t sections;

    sections = 0;
    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 2);
}

static void test_rejects_out_of_order_sections(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x03, 0x00, 0x01, 0x00 };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_overlong_leb128(void) {
    const uint8_t bytes[] = {
        WASM_HEADER, 0x01, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00
    };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_rejects_length_that_overflows(void) {
    const uint8_t bytes[] = {
        WASM_HEADER, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F
    };
    uint32_t sections;

    CHECK(!jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
}

static void test_multi_byte_leb128_length(void) {
    uint8_t bytes[8 + 3 + 200];
    uint32_t sections;

    memset(bytes, 0, sizeof(bytes));
    {
        const uint8_t header[] = { WASM_HEADER };

        memcpy(bytes, header, sizeof(header));
    }
    bytes[8] = 0x01;
    bytes[9] = 0xC8;
    bytes[10] = 0x01;

    sections = 0;
    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), &sections));
    CHECK(sections == 1);
}

static void test_null_section_count_is_allowed(void) {
    const uint8_t bytes[] = { WASM_HEADER, 0x01, 0x00 };

    CHECK(jani_wasm_module_validate(bytes, sizeof(bytes), 0));
}

int main(void) {
    test_rejects_short_input();
    test_rejects_bad_magic();
    test_rejects_bad_version();
    test_accepts_header_only_module();
    test_accepts_one_empty_section();
    test_accepts_section_with_payload();
    test_rejects_truncated_section_payload();
    test_rejects_missing_section_length();
    test_rejects_unknown_section_id();
    test_rejects_duplicate_non_custom_section();
    test_allows_repeated_custom_sections();
    test_rejects_out_of_order_sections();
    test_rejects_overlong_leb128();
    test_rejects_length_that_overflows();
    test_multi_byte_leb128_length();
    test_null_section_count_is_allowed();
    printf("test_wasm_module: %lu checks passed\n", checks_passed);
    return 0;
}
