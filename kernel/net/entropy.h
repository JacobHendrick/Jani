#ifndef JANI_KERNEL_NET_ENTROPY_H
#define JANI_KERNEL_NET_ENTROPY_H
#include <stddef.h>
#include <stdint.h>
/* x86 CPU RNG is an explicit hardware trust assumption. No clock fallback.
 * Success fills the complete buffer; failure leaves it unchanged. */
int net_cpu_entropy(void *context, uint8_t *out, size_t length);
#endif
