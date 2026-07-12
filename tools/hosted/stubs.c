/* Hosted stand-ins for kernel services (blueprint rule R3).
 *
 * kernel/lib/string.c is deliberately NOT compiled hosted: libc provides
 * memset/memcpy here, and ASan intercepts libc's versions - which is
 * exactly the bounds coverage we want on every byte the code touches. */
#include <stdarg.h>
#include <stdio.h>

void kputs(const char *str) {
    fputs(str, stdout);
}

void printk(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
