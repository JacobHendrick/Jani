#include "jani_libc.h"

#define JANI_DOUBLE_ALL_INTEGRAL 9007199254740992.0
#define JANI_FLOAT_ALL_INTEGRAL 16777216.0f

double jani_sqrt(double value) {
    double result;

    __asm__ ("sqrtsd %1, %0" : "=x"(result) : "x"(value));

    return result;
}

double jani_fabs(double value) {
    return __builtin_fabs(value);
}

static int is_not_finite(double value) {
    return __builtin_isnan(value) || __builtin_isinf(value);
}

static int is_already_integral(double value) {
    return (value >= JANI_DOUBLE_ALL_INTEGRAL)
        || (value <= -JANI_DOUBLE_ALL_INTEGRAL);
}

static double with_zero_sign_of(double result, double value) {
    if (result == 0.0) {
        return __builtin_copysign(0.0, value);
    }
    return result;
}

double jani_trunc(double value) {
    if (is_not_finite(value) || is_already_integral(value)) {
        return value;
    }

    return with_zero_sign_of((double)(long long)value, value);
}

double jani_floor(double value) {
    double truncated;

    if (is_not_finite(value) || is_already_integral(value)) {
        return value;
    }

    truncated = (double)(long long)value;

    if ((truncated > value)) {
        truncated = truncated - 1.0;
    }

    return with_zero_sign_of(truncated, value);
}

double jani_ceil(double value) {
    double truncated;

    if (is_not_finite(value) || is_already_integral(value)) {
        return value;
    }

    truncated = (double)(long long)value;

    if ((truncated < value)) {
        truncated = truncated + 1.0;
    }

    return with_zero_sign_of(truncated, value);
}

double jani_rint(double value) {
    double truncated;
    double difference;
    double result;

    if (is_not_finite(value) || is_already_integral(value)) {
        return value;
    }

    truncated = (double)(long long)value;
    difference = value - truncated;
    result = truncated;

    if (difference > 0.5) {
        result = truncated + 1.0;
    } else if (difference < -0.5) {
        result = truncated - 1.0;
    } else if ((difference == 0.5) || (difference == -0.5)) {
        long long integral;

        integral = (long long)truncated;

        if ((integral % 2) != 0) {
            result = (difference > 0.0) ? (truncated + 1.0)
                                        : (truncated - 1.0);
        }
    }

    return with_zero_sign_of(result, value);
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
    float result;

    __asm__ ("sqrtss %1, %0" : "=x"(result) : "x"(value));

    return result;
}

float jani_fabsf(float value) {
    return __builtin_fabsf(value);
}

float jani_ceilf(float value) {
    if (__builtin_isnan(value) || __builtin_isinf(value)
        || (value >= JANI_FLOAT_ALL_INTEGRAL)
        || (value <= -JANI_FLOAT_ALL_INTEGRAL)) {
        return value;
    }

    return (float)jani_ceil((double)value);
}

float jani_floorf(float value) {
    if (__builtin_isnan(value) || __builtin_isinf(value)
        || (value >= JANI_FLOAT_ALL_INTEGRAL)
        || (value <= -JANI_FLOAT_ALL_INTEGRAL)) {
        return value;
    }

    return (float)jani_floor((double)value);
}

float jani_truncf(float value) {
    if (__builtin_isnan(value) || __builtin_isinf(value)
        || (value >= JANI_FLOAT_ALL_INTEGRAL)
        || (value <= -JANI_FLOAT_ALL_INTEGRAL)) {
        return value;
    }

    return (float)jani_trunc((double)value);
}

float jani_rintf(float value) {
    if (__builtin_isnan(value) || __builtin_isinf(value)
        || (value >= JANI_FLOAT_ALL_INTEGRAL)
        || (value <= -JANI_FLOAT_ALL_INTEGRAL)) {
        return value;
    }

    return (float)jani_rint((double)value);
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

int jani_signbit(double value) {
    return __builtin_signbit(value);
}

void jani_abort(const char *file, int line, const char *expression) {
    jani_printf("ASSERT FAILED %s:%d: %s\n", file, line, expression);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
