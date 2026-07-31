#include "jani_libc.h"

double jani_sqrt(double value) {
    return __builtin_sqrt(value);
}

double jani_fabs(double value) {
    return __builtin_fabs(value);
}

double jani_ceil(double value) {
    return __builtin_ceil(value);
}

double jani_floor(double value) {
    return __builtin_floor(value);
}

double jani_trunc(double value) {
    return __builtin_trunc(value);
}

double jani_rint(double value) {
    return __builtin_rint(value);
}

double jani_fmin(double left, double right) {
    return __builtin_fmin(left, right);
}

double jani_fmax(double left, double right) {
    return __builtin_fmax(left, right);
}

double jani_copysign(double magnitude, double sign) {
    return __builtin_copysign(magnitude, sign);
}

int jani_isnan(double value) {
    return __builtin_isnan(value);
}

int jani_isinf(double value) {
    return __builtin_isinf(value);
}

float jani_sqrtf(float value) {
    return __builtin_sqrtf(value);
}

float jani_fabsf(float value) {
    return __builtin_fabsf(value);
}

float jani_ceilf(float value) {
    return __builtin_ceilf(value);
}

float jani_floorf(float value) {
    return __builtin_floorf(value);
}

float jani_truncf(float value) {
    return __builtin_truncf(value);
}

float jani_rintf(float value) {
    return __builtin_rintf(value);
}

float jani_fminf(float left, float right) {
    return __builtin_fminf(left, right);
}

float jani_fmaxf(float left, float right) {
    return __builtin_fmaxf(left, right);
}

float jani_copysignf(float magnitude, float sign) {
    return __builtin_copysignf(magnitude, sign);
}

void jani_abort(const char *file, int line, const char *expression) {
    jani_printf("ASSERT FAILED %s:%d: %s\n", file, line, expression);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
