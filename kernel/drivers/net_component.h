#ifndef JANI_KERNEL_DRIVERS_NET_COMPONENT_H
#define JANI_KERNEL_DRIVERS_NET_COMPONENT_H
#include "../wasm/component.h"
#include "../net/frame_decode.h"
struct net_frame_result {
    uint32_t source, destination;
    uint32_t source_port, destination_port;
    uint32_t payload_offset, payload_length, reserved;
};
_Static_assert(sizeof(struct net_frame_result) == 28, "network driver response ABI");
int net_component_start(void);
int net_component_validate(const uint8_t *, size_t, struct jani_udp_datagram *);
int net_component_request(struct component *, uint8_t *, uint32_t);
int net_component_complete(struct component *, int32_t, const uint8_t *, uint32_t);
void net_component_stop(void);
#endif
