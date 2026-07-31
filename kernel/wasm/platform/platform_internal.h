#ifndef JANI_KERNEL_WASM_PLATFORM_INTERNAL_H
#define JANI_KERNEL_WASM_PLATFORM_INTERNAL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BH_PLATFORM_JANI
#define BH_PLATFORM_JANI
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)

#define BH_THREAD_DEFAULT_PRIORITY 0

typedef uint64_t korp_thread;
typedef uint64_t korp_tid;
typedef uint64_t korp_sem;

typedef struct korp_mutex {
    int locked;
} korp_mutex;

typedef struct korp_cond {
    int waiters;
} korp_cond;

typedef struct korp_rwlock {
    int locked;
} korp_rwlock;

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

static inline int
os_getpagesize(void)
{
    return 4096;
}

#endif
