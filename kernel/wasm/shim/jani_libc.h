#ifndef JANI_KERNEL_WASM_SHIM_JANI_LIBC_H
#define JANI_KERNEL_WASM_SHIM_JANI_LIBC_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

int jani_memcmp(const void *left, const void *right, size_t count);
int jani_strcmp(const char *left, const char *right);
int jani_strncmp(const char *left, const char *right, size_t count);
char *jani_strcpy(char *destination, const char *source);
char *jani_strncpy(char *destination, const char *source, size_t count);
char *jani_strchr(const char *text, int character);
char *jani_strrchr(const char *text, int character);
char *jani_strstr(const char *haystack, const char *needle);

void *jani_malloc(size_t size);
void *jani_calloc(size_t count, size_t size);
void *jani_realloc(void *memory, size_t size);
void jani_free(void *memory);
int jani_abs(int value);
long jani_strtol(const char *text, char **end, int base);
unsigned long jani_strtoul(const char *text, char **end, int base);

int jani_snprintf(char *buffer, size_t size, const char *format, ...);
int jani_vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int jani_printf(const char *format, ...);
int jani_vprintf(const char *format, va_list args);
int jani_putchar(int character);
int jani_puts(const char *text);

double jani_sqrt(double value);
double jani_fabs(double value);
double jani_ceil(double value);
double jani_floor(double value);
double jani_trunc(double value);
double jani_rint(double value);
double jani_fmin(double left, double right);
double jani_fmax(double left, double right);
double jani_copysign(double magnitude, double sign);
int jani_isnan(double value);
int jani_isinf(double value);

float jani_sqrtf(float value);
float jani_fabsf(float value);
float jani_ceilf(float value);
float jani_floorf(float value);
float jani_truncf(float value);
float jani_rintf(float value);
float jani_fminf(float left, float right);
float jani_fmaxf(float left, float right);
float jani_copysignf(float magnitude, float sign);

void jani_abort(const char *file, int line, const char *expression);

#endif
