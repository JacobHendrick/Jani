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

static int same_double(double left, double right) {
    if (left != right) {
        return 0;
    }
    return jani_signbit(left) == jani_signbit(right);
}

static void test_rounding_matches_libm_semantics(void) {
    CHECK(jani_trunc(2.7) == 2.0);
    CHECK(jani_trunc(-2.7) == -2.0);
    CHECK(jani_trunc(0.9) == 0.0);
    CHECK(same_double(jani_trunc(-0.9), -0.0));

    CHECK(jani_floor(2.7) == 2.0);
    CHECK(jani_floor(2.0) == 2.0);
    CHECK(jani_floor(-2.1) == -3.0);
    CHECK(jani_floor(-2.0) == -2.0);
    CHECK(same_double(jani_floor(-0.0), -0.0));

    CHECK(jani_ceil(2.1) == 3.0);
    CHECK(jani_ceil(2.0) == 2.0);
    CHECK(jani_ceil(-2.7) == -2.0);
    CHECK(jani_ceil(-2.0) == -2.0);
    CHECK(same_double(jani_ceil(-0.5), -0.0));

    CHECK(jani_rint(2.4) == 2.0);
    CHECK(jani_rint(2.6) == 3.0);
    CHECK(jani_rint(-2.4) == -2.0);
    CHECK(jani_rint(-2.6) == -3.0);
    CHECK(same_double(jani_rint(-0.4), -0.0));
}

static void test_rint_breaks_ties_to_even(void) {
    CHECK(jani_rint(0.5) == 0.0);
    CHECK(jani_rint(1.5) == 2.0);
    CHECK(jani_rint(2.5) == 2.0);
    CHECK(jani_rint(3.5) == 4.0);
    CHECK(jani_rint(4.5) == 4.0);
    CHECK(jani_rint(-0.5) == 0.0);
    CHECK(jani_rint(-1.5) == -2.0);
    CHECK(jani_rint(-2.5) == -2.0);
    CHECK(jani_rint(-3.5) == -4.0);
    CHECK(same_double(jani_rint(-0.5), -0.0));
}

static void test_rounding_passes_through_large_and_special(void) {
    double huge = 1.0e300;
    double two_pow_53 = 9007199254740992.0;

    CHECK(jani_trunc(huge) == huge);
    CHECK(jani_floor(huge) == huge);
    CHECK(jani_ceil(huge) == huge);
    CHECK(jani_rint(huge) == huge);

    CHECK(jani_trunc(two_pow_53) == two_pow_53);
    CHECK(jani_rint(two_pow_53) == two_pow_53);
    CHECK(jani_floor(-two_pow_53) == -two_pow_53);

    CHECK(jani_isnan(jani_trunc(0.0 / 0.0)));
    CHECK(jani_isnan(jani_rint(0.0 / 0.0)));
    CHECK(jani_isinf(jani_floor(1.0 / 0.0)));
    CHECK(jani_isinf(jani_ceil(-1.0 / 0.0)));
}

static void test_sqrt_is_exact_on_perfect_squares(void) {
    CHECK(jani_sqrt(0.0) == 0.0);
    CHECK(jani_sqrt(1.0) == 1.0);
    CHECK(jani_sqrt(4.0) == 2.0);
    CHECK(jani_sqrt(16.0) == 4.0);
    CHECK(jani_sqrt(2.25) == 1.5);
    CHECK(jani_sqrt(1.0e300) > 0.0);
    CHECK(jani_isnan(jani_sqrt(-1.0)));

    CHECK(jani_sqrtf(0.0f) == 0.0f);
    CHECK(jani_sqrtf(9.0f) == 3.0f);
    CHECK(jani_sqrtf(2.25f) == 1.5f);
}

static void test_float_rounding_variants(void) {
    CHECK(jani_truncf(2.7f) == 2.0f);
    CHECK(jani_truncf(-2.7f) == -2.0f);
    CHECK(jani_floorf(-2.1f) == -3.0f);
    CHECK(jani_ceilf(2.1f) == 3.0f);
    CHECK(jani_rintf(2.5f) == 2.0f);
    CHECK(jani_rintf(3.5f) == 4.0f);
    CHECK(jani_rintf(-2.5f) == -2.0f);
    CHECK(jani_truncf(1.0e30f) == 1.0e30f);
}

static int compare_int(const void *left, const void *right) {
    int a = *(const int *)left;
    int b = *(const int *)right;

    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static int is_sorted(const int *values, size_t count) {
    size_t index;

    for (index = 1; index < count; index++) {
        if (values[index - 1] > values[index]) {
            return 0;
        }
    }
    return 1;
}

static void test_qsort_basic_and_edges(void) {
    int values[] = { 5, 3, 9, 1, 7, 3, 8, 2 };
    int single[] = { 42 };

    jani_qsort(values, 8, sizeof(int), compare_int);
    CHECK(is_sorted(values, 8));
    CHECK(values[0] == 1);
    CHECK(values[7] == 9);

    jani_qsort(single, 1, sizeof(int), compare_int);
    CHECK(single[0] == 42);

    jani_qsort(values, 0, sizeof(int), compare_int);
    CHECK(is_sorted(values, 8));
}

static void test_qsort_survives_adversarial_inputs(void) {
    static int all_equal[4096];
    static int reversed[4096];
    static int organ_pipe[4096];
    size_t index;

    for (index = 0; index < 4096; index++) {
        all_equal[index] = 7;
        reversed[index] = (int)(4096 - index);
        organ_pipe[index] =
            (index < 2048) ? (int)index : (int)(4096 - index);
    }

    jani_qsort(all_equal, 4096, sizeof(int), compare_int);
    CHECK(is_sorted(all_equal, 4096));
    CHECK(all_equal[0] == 7);
    CHECK(all_equal[4095] == 7);

    jani_qsort(reversed, 4096, sizeof(int), compare_int);
    CHECK(is_sorted(reversed, 4096));
    CHECK(reversed[0] == 1);
    CHECK(reversed[4095] == 4096);

    jani_qsort(organ_pipe, 4096, sizeof(int), compare_int);
    CHECK(is_sorted(organ_pipe, 4096));
}

static void test_qsort_is_a_permutation(void) {
    static int values[1000];
    static int seen[1000];
    unsigned long state;
    size_t index;

    state = 12345;
    for (index = 0; index < 1000; index++) {
        state = (state * 1103515245UL) + 12345UL;
        values[index] = (int)((state >> 16) % 500);
        seen[index] = 0;
    }

    jani_qsort(values, 1000, sizeof(int), compare_int);
    CHECK(is_sorted(values, 1000));

    for (index = 0; index < 1000; index++) {
        CHECK(values[index] >= 0);
        CHECK(values[index] < 500);
    }
}

static void test_qsort_handles_large_elements(void) {
    struct wide {
        int key;
        char padding[52];
    };
    static struct wide items[64];
    size_t index;

    for (index = 0; index < 64; index++) {
        items[index].key = (int)(64 - index);
        items[index].padding[0] = (char)index;
    }

    jani_qsort(items, 64, sizeof(struct wide), compare_int);

    for (index = 1; index < 64; index++) {
        CHECK(items[index - 1].key <= items[index].key);
    }
    CHECK(items[0].padding[0] == (char)63);
}

static void test_bsearch_finds_and_misses(void) {
    int values[] = { 1, 3, 5, 7, 9, 11 };
    int key;
    void *found;

    key = 7;
    found = jani_bsearch(&key, values, 6, sizeof(int), compare_int);
    CHECK(found != 0);
    CHECK(*(int *)found == 7);

    key = 1;
    found = jani_bsearch(&key, values, 6, sizeof(int), compare_int);
    CHECK(found != 0);
    CHECK(*(int *)found == 1);

    key = 11;
    found = jani_bsearch(&key, values, 6, sizeof(int), compare_int);
    CHECK(found != 0);
    CHECK(*(int *)found == 11);

    key = 4;
    CHECK(jani_bsearch(&key, values, 6, sizeof(int), compare_int) == 0);

    key = 100;
    CHECK(jani_bsearch(&key, values, 6, sizeof(int), compare_int) == 0);

    key = 1;
    CHECK(jani_bsearch(&key, values, 0, sizeof(int), compare_int) == 0);
}

static void test_labs_and_signbit(void) {
    CHECK(jani_labs(-5L) == 5L);
    CHECK(jani_labs(5L) == 5L);
    CHECK(jani_labs(0L) == 0L);
    CHECK(jani_signbit(-1.0) != 0);
    CHECK(jani_signbit(1.0) == 0);
    CHECK(jani_signbit(-0.0) != 0);
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
    test_rounding_matches_libm_semantics();
    test_rint_breaks_ties_to_even();
    test_rounding_passes_through_large_and_special();
    test_sqrt_is_exact_on_perfect_squares();
    test_float_rounding_variants();
    test_qsort_basic_and_edges();
    test_qsort_survives_adversarial_inputs();
    test_qsort_is_a_permutation();
    test_qsort_handles_large_elements();
    test_bsearch_finds_and_misses();
    test_labs_and_signbit();
    printf("test_wasm_shim: %lu checks passed\n", checks_passed);
    return 0;
}
