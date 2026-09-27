#ifndef JANI_KERNEL_NET_CONFIG_H
#define JANI_KERNEL_NET_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#define NET_CONFIG_BYTES 104u
struct net_boot_config {
    uint32_t role, reserved;
    uint8_t seed[32], peer_key[32], local_key[32];
};
/* Trusted local provisioning module, not a network message. */
int net_config_decode(const uint8_t *bytes, size_t length, struct net_boot_config *out);
#endif
