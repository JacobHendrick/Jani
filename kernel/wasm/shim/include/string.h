#ifndef JANI_WASM_SHIM_STRING_H
#define JANI_WASM_SHIM_STRING_H

#include <stddef.h>

#include "../jani_libc.h"
#include "../../../lib/string.h"

#define memcmp jani_memcmp
#define strcmp jani_strcmp
#define strncmp jani_strncmp
#define strcpy jani_strcpy
#define strncpy jani_strncpy
#define strchr jani_strchr
#define strrchr jani_strrchr
#define strstr jani_strstr

#endif
