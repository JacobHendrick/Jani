#ifndef JANI_WASM_SHIM_STDLIB_H
#define JANI_WASM_SHIM_STDLIB_H

#include <stddef.h>

#include "../jani_libc.h"

#define malloc jani_malloc
#define calloc jani_calloc
#define realloc jani_realloc
#define free jani_free
#define abs jani_abs
#define strtol jani_strtol
#define strtoul jani_strtoul

#endif
