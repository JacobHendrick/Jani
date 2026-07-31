#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/wasm/shim/jani_libc.h"
#include "check.h"

unsigned long checks_passed;

static void test_snprintf_returns_would_be_length(void) {
    char buffer[8];

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%s", "abcdefghijkl") == 12);
    CHECK(buffer[7] == '\0');
    CHECK(buffer[0] == 'a');
    CHECK(buffer[6] == 'g');
}

static void test_snprintf_exact_fit(void) {
    char buffer[4];

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%d", 123) == 3);
    CHECK(buffer[3] == '\0');
    CHECK(strcmp(buffer, "123") == 0);
}

static void test_snprintf_zero_size_never_writes(void) {
    char buffer[1];

    buffer[0] = 0x7F;
    CHECK(jani_snprintf(buffer, 0, "%d", 5) == 1);
    CHECK(buffer[0] == 0x7F);
}

static void test_snprintf_integers(void) {
    char buffer[32];

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%d", 0) == 1);
    CHECK(strcmp(buffer, "0") == 0);

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%d", -42) == 3);
    CHECK(strcmp(buffer, "-42") == 0);

    jani_snprintf(buffer, sizeof(buffer), "%d", -2147483647 - 1);
    CHECK(strcmp(buffer, "-2147483648") == 0);

    jani_snprintf(buffer, sizeof(buffer), "%u", 4294967295u);
    CHECK(strcmp(buffer, "4294967295") == 0);

    jani_snprintf(buffer, sizeof(buffer), "%x", 0xDEADBEEFu);
    CHECK(strcmp(buffer, "deadbeef") == 0);

    jani_snprintf(buffer, sizeof(buffer), "%llu", 18446744073709551615ull);
    CHECK(strcmp(buffer, "18446744073709551615") == 0);
}

static void test_snprintf_strings_and_chars(void) {
    char buffer[32];

    CHECK(jani_snprintf(buffer, sizeof(buffer), "a%sb", "XY") == 4);
    CHECK(strcmp(buffer, "aXYb") == 0);

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%c%c", 'h', 'i') == 2);
    CHECK(strcmp(buffer, "hi") == 0);

    CHECK(jani_snprintf(buffer, sizeof(buffer), "%s", (char *)0) == 6);
    CHECK(strcmp(buffer, "(null)") == 0);
}

static void test_snprintf_percent_and_unknown_specifier(void) {
    char buffer[32];

    CHECK(jani_snprintf(buffer, sizeof(buffer), "100%%") == 4);
    CHECK(strcmp(buffer, "100%") == 0);

    jani_snprintf(buffer, sizeof(buffer), "%q", 1);
    CHECK(strcmp(buffer, "%q") == 0);

    jani_snprintf(buffer, sizeof(buffer), "trailing%");
    CHECK(strcmp(buffer, "trailing%") == 0);
}

static void test_snprintf_truncation_is_always_terminated(void) {
    char buffer[5];
    size_t index;

    for (index = 1; index < sizeof(buffer); index++) {
        memset(buffer, 0x7F, sizeof(buffer));
        jani_snprintf(buffer, index, "%s", "0123456789");
        CHECK(buffer[index - 1] == '\0');
        CHECK(buffer[index] == 0x7F);
    }
}

static void test_string_functions(void) {
    char destination[8];

    CHECK(jani_memcmp("abc", "abc", 3) == 0);
    CHECK(jani_memcmp("abc", "abd", 3) < 0);
    CHECK(jani_memcmp("abd", "abc", 3) > 0);
    CHECK(jani_memcmp("", "", 0) == 0);

    CHECK(jani_strcmp("abc", "abc") == 0);
    CHECK(jani_strcmp("abc", "abd") < 0);
    CHECK(jani_strcmp("abc", "ab") > 0);
    CHECK(jani_strcmp("", "") == 0);

    CHECK(jani_strncmp("abcXX", "abcYY", 3) == 0);
    CHECK(jani_strncmp("abcXX", "abdYY", 3) < 0);
    CHECK(jani_strncmp("abc", "abc", 0) == 0);

    CHECK(jani_strcpy(destination, "hello") == destination);
    CHECK(strcmp(destination, "hello") == 0);

    memset(destination, 0x7F, sizeof(destination));
    jani_strncpy(destination, "ab", 5);
    CHECK(destination[0] == 'a');
    CHECK(destination[1] == 'b');
    CHECK(destination[2] == '\0');
    CHECK(destination[3] == '\0');
    CHECK(destination[4] == '\0');
    CHECK(destination[5] == 0x7F);

    CHECK(jani_strchr("hello", 'l') != 0);
    CHECK(*jani_strchr("hello", 'l') == 'l');
    CHECK(jani_strchr("hello", 'z') == 0);
    CHECK(jani_strchr("hello", '\0') != 0);

    CHECK(jani_strrchr("hello", 'l') != 0);
    CHECK(jani_strrchr("hello", 'l')[1] == 'o');
}

static void test_strtol(void) {
    char *end;

    CHECK(jani_strtol("123", &end, 10) == 123);
    CHECK(*end == '\0');

    CHECK(jani_strtol("-45rest", &end, 10) == -45);
    CHECK(strcmp(end, "rest") == 0);

    CHECK(jani_strtol("  7", &end, 10) == 7);
    CHECK(jani_strtol("ff", &end, 16) == 255);
    CHECK(jani_strtol("0x1A", &end, 16) == 26);
    CHECK(jani_strtol("zzz", &end, 10) == 0);
}

static void test_math_helpers(void) {
    CHECK(jani_fabs(-3.5) == 3.5);
    CHECK(jani_fabs(3.5) == 3.5);
    CHECK(jani_sqrt(16.0) == 4.0);
    CHECK(jani_floor(2.7) == 2.0);
    CHECK(jani_floor(-2.1) == -3.0);
    CHECK(jani_ceil(2.1) == 3.0);
    CHECK(jani_ceil(-2.7) == -2.0);
    CHECK(jani_trunc(2.7) == 2.0);
    CHECK(jani_trunc(-2.7) == -2.0);
    CHECK(jani_copysign(3.0, -1.0) == -3.0);
    CHECK(jani_fmin(1.0, 2.0) == 1.0);
    CHECK(jani_fmax(1.0, 2.0) == 2.0);
    CHECK(jani_isnan(0.0) == 0);
    CHECK(jani_isinf(0.0) == 0);
}

int main(void) {
    test_snprintf_returns_would_be_length();
    test_snprintf_exact_fit();
    test_snprintf_zero_size_never_writes();
    test_snprintf_integers();
    test_snprintf_strings_and_chars();
    test_snprintf_percent_and_unknown_specifier();
    test_snprintf_truncation_is_always_terminated();
    test_string_functions();
    test_strtol();
    test_math_helpers();
    printf("test_wasm_shim: %lu checks passed\n", checks_passed);
    return 0;
}
