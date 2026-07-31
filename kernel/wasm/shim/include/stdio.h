#ifndef JANI_WASM_SHIM_STDIO_H
#define JANI_WASM_SHIM_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#include "../jani_libc.h"

#define snprintf jani_snprintf
#define vsnprintf jani_vsnprintf
#define printf jani_printf
#define vprintf jani_vprintf
#define putchar jani_putchar
#define puts jani_puts

#endif
