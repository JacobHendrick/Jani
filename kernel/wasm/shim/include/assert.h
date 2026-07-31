#ifndef JANI_WASM_SHIM_ASSERT_H
#define JANI_WASM_SHIM_ASSERT_H

#include "../jani_libc.h"

#define assert(expression) \
    ((expression) ? (void)0 : jani_abort(__FILE__, __LINE__, #expression))

#endif
