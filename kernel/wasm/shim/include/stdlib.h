#ifndef JANI_WASM_SHIM_STDLIB_H
#define JANI_WASM_SHIM_STDLIB_H

#include <stddef.h>

#include "../jani_libc.h"

#define malloc jani_malloc
#define calloc jani_calloc
#define realloc jani_realloc
#define free jani_free
#define abs jani_abs
#define labs jani_labs
#define strtol jani_strtol
#define strtoul jani_strtoul
#define bsearch jani_bsearch
#define qsort jani_qsort

#define abort() jani_abort(__FILE__, __LINE__, "abort")

#endif
