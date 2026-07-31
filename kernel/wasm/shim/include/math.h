#ifndef JANI_WASM_SHIM_MATH_H
#define JANI_WASM_SHIM_MATH_H

#include "../jani_libc.h"

#define sqrt jani_sqrt
#define fabs jani_fabs
#define ceil jani_ceil
#define floor jani_floor
#define trunc jani_trunc
#define rint jani_rint
#define fmin jani_fmin
#define fmax jani_fmax
#define copysign jani_copysign
#define isnan jani_isnan
#define isinf jani_isinf
#define signbit jani_signbit

#define sqrtf jani_sqrtf
#define fabsf jani_fabsf
#define ceilf jani_ceilf
#define floorf jani_floorf
#define truncf jani_truncf
#define rintf jani_rintf
#define fminf jani_fminf
#define fmaxf jani_fmaxf
#define copysignf jani_copysignf

#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()

#endif
