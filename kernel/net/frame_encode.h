#ifndef JANI_KERNEL_NET_FRAME_ENCODE_H
#define JANI_KERNEL_NET_FRAME_ENCODE_H

#include <stddef.h>
#include <stdint.h>

struct jani_udp_frame_fields {
    uint8_t destination_mac[6];
    uint8_t source_mac[6];
    uint8_t source_ip[4];
    uint8_t destination_ip[4];
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t identification;
    uint8_t ttl;
};

/* Kernel buffers only. Returns 60..1514 bytes or 0 without modifying output.
 * A null payload is allowed only for length zero. Payload/output cannot overlap. */
size_t jani_udp_frame_encode(uint8_t *output, size_t capacity,
                             const struct jani_udp_frame_fields *fields,
                             const uint8_t *payload, size_t payload_length);

#endif
