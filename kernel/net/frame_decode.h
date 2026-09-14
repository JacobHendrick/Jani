#ifndef JANI_KERNEL_NET_FRAME_DECODE_H
#define JANI_KERNEL_NET_FRAME_DECODE_H

#include <stddef.h>
#include <stdint.h>

struct jani_udp_datagram {
    uint8_t source[4];
    uint8_t destination[4];
    uint16_t source_port;
    uint16_t destination_port;
    size_t payload_offset;
    size_t payload_length;
};

/* Validates packet structure, not peer identity. Returns 1 on success and 0
 * on failure. Failure leaves out unchanged. The payload remains in frame. */
int jani_udp_frame_decode(const uint8_t *frame, size_t length,
                          struct jani_udp_datagram *out);

#endif // JANI_KERNEL_NET_FRAME_DECODE_H
