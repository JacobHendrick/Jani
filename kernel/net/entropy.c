#include "entropy.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"

int net_cpu_entropy(void *context, uint8_t *out, size_t length) {
    (void)context;
    if (out == NULL || length == 0 || length > 64) return 0;
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 1) return 0;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if ((c & (UINT32_C(1) << 30)) == 0) return 0;
    uint8_t temporary[64] = {0};
    for (size_t offset = 0; offset < length; offset += 8) {
        uint64_t value = 0;
        unsigned char valid = 0;
        for (unsigned int attempt = 0; attempt < 32 && !valid; attempt++)
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(value), "=qm"(valid));
        if (!valid) { crypto_wipe(temporary, sizeof(temporary)); return 0; }
        size_t remaining = length - offset;
        memcpy(temporary + offset, &value, remaining < 8 ? remaining : 8);
        crypto_wipe(&value, sizeof(value));
    }
    memcpy(out, temporary, length);
    crypto_wipe(temporary, sizeof(temporary)); return 1;
}
