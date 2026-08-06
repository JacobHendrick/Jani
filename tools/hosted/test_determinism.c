#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"

unsigned long checks_passed;

#define F32_SIGN UINT32_C(0x80000000)
#define F32_CANONICAL UINT32_C(0x7FC00000)
#define F32_QUIET UINT32_C(0x00400000)
#define F32_PAYLOAD UINT32_C(0x003FFFFF)
#define F32_INFINITY UINT32_C(0x7F800000)

#define F64_SIGN UINT64_C(0x8000000000000000)
#define F64_CANONICAL UINT64_C(0x7FF8000000000000)
#define F64_INFINITY UINT64_C(0x7FF0000000000000)

static volatile float f32_lhs;
static volatile float f32_rhs;
static volatile double f64_lhs;
static volatile double f64_rhs;

static uint32_t f32_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float f32_from(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t f64_bits(double value) {
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double f64_from(uint64_t bits) {
    double value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void test_f32_generated_nan_is_canonical(void) {
    uint32_t quotient;
    uint32_t difference;

    f32_lhs = 0.0f;
    f32_rhs = 0.0f;
    quotient = f32_bits(f32_lhs / f32_rhs);

    f32_lhs = f32_from(F32_INFINITY);
    f32_rhs = f32_from(F32_INFINITY);
    difference = f32_bits(f32_lhs - f32_rhs);

    CHECK((quotient & ~F32_SIGN) == F32_CANONICAL);
    CHECK((difference & ~F32_SIGN) == F32_CANONICAL);
    CHECK((quotient & F32_PAYLOAD) == 0);
    CHECK((difference & F32_PAYLOAD) == 0);
    CHECK(quotient == difference);
}

static void test_f64_generated_nan_is_canonical(void) {
    uint64_t quotient;
    uint64_t difference;

    f64_lhs = 0.0;
    f64_rhs = 0.0;
    quotient = f64_bits(f64_lhs / f64_rhs);

    f64_lhs = f64_from(F64_INFINITY);
    f64_rhs = f64_from(F64_INFINITY);
    difference = f64_bits(f64_lhs - f64_rhs);

    CHECK((quotient & ~F64_SIGN) == F64_CANONICAL);
    CHECK((difference & ~F64_SIGN) == F64_CANONICAL);
    CHECK(quotient == difference);
}

static void test_generated_nan_is_repeatable(void) {
    uint32_t first;
    unsigned int round;

    f32_lhs = 0.0f;
    f32_rhs = 0.0f;
    first = f32_bits(f32_lhs / f32_rhs);

    for (round = 0; round < 4096u; round++) {
        f32_lhs = 0.0f;
        f32_rhs = 0.0f;
        CHECK(f32_bits(f32_lhs / f32_rhs) == first);
    }
}

static void test_f32_propagates_operand_payload(void) {
    f32_lhs = f32_from(F32_CANONICAL | UINT32_C(0x00001234));
    f32_rhs = 1.0f;
    CHECK(f32_bits(f32_lhs + f32_rhs) == (F32_CANONICAL | UINT32_C(0x00001234)));

    f32_lhs = f32_from(F32_SIGN | F32_CANONICAL | UINT32_C(0x0000ABCD));
    f32_rhs = 1.0f;
    CHECK(f32_bits(f32_lhs / f32_rhs) ==
          (F32_SIGN | F32_CANONICAL | UINT32_C(0x0000ABCD)));
}

static void test_f32_quiets_signalling_payload(void) {
    uint32_t signalling;

    signalling = F32_INFINITY | UINT32_C(0x00205678);

    f32_lhs = f32_from(signalling);
    f32_rhs = 1.0f;

    CHECK((signalling & F32_QUIET) == 0);
    CHECK(f32_bits(f32_lhs * f32_rhs) == (signalling | F32_QUIET));
}

static void test_f32_propagation_picks_first_operand(void) {
    f32_lhs = f32_from(F32_CANONICAL | UINT32_C(0x00001111));
    f32_rhs = f32_from(F32_CANONICAL | UINT32_C(0x00002222));

    CHECK(f32_bits(f32_lhs + f32_rhs) == (F32_CANONICAL | UINT32_C(0x00001111)));
}

int main(void) {
    test_f32_generated_nan_is_canonical();
    test_f64_generated_nan_is_canonical();
    test_generated_nan_is_repeatable();
    test_f32_propagates_operand_payload();
    test_f32_quiets_signalling_payload();
    test_f32_propagation_picks_first_operand();

    printf("test_determinism: %lu checks passed\n", checks_passed);
    return 0;
}
