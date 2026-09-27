#ifndef JANI_KERNEL_NET_PEER_H
#define JANI_KERNEL_NET_PEER_H
#include "replica.h"
#include "frame_decode.h"
#include "frame_encode.h"

typedef int (*net_peer_receiver)(void *, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
typedef size_t (*net_peer_sender)(void *, uint8_t *, size_t);
struct net_peer {
    struct net_session session;
    struct replica replica;
    struct jani_udp_frame_fields route;
    uint64_t hello_tick, summary_tick;
    size_t pending_length;
    uint32_t failed;
    uint8_t pending[NET_PACKET_MAX];
    void *extension;
    net_peer_receiver receive_extension;
    net_peer_sender send_extension;
    int (*validate_packet)(const uint8_t *, size_t, struct jani_udp_datagram *);
};
int net_peer_start(struct net_peer *peer, struct object_store *store,
    const struct net_identity *identity, const uint8_t remote[32],
    const uint8_t fresh_exchange_secret[32], const struct jani_udp_frame_fields *route);
/* One transport per kernel. Caller polls regularly with monotonic PIT ticks.
 * Received DMA is released on all paths; busy output is retained until copied.
 * Returns -1 on driver/storage quarantine; 1 otherwise, including bad packets. */
int net_peer_extension(struct net_peer *, void *, net_peer_receiver, net_peer_sender);
int net_peer_step(struct net_peer *peer, uint64_t ticks);
#endif
