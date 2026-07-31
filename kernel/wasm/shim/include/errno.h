#ifndef JANI_WASM_SHIM_ERRNO_H
#define JANI_WASM_SHIM_ERRNO_H

extern int jani_errno;

#define errno jani_errno

#define EINVAL 22
#define ENOMEM 12
#define ERANGE 34

#endif
