#include "peer.h"
#include "transmit.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"

static int net_bytes_compare(const void *left, const void *right, size_t length) {
    const uint8_t *a = left, *b = right;
    for (size_t i = 0; i < length; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
int net_peer_start(struct net_peer *peer, struct object_store *store,
    const struct net_identity *identity, const uint8_t remote[32],
    const uint8_t secret[32], const struct jani_udp_frame_fields *route) {
    if (peer == NULL || store == NULL || route == NULL || route->ttl == 0 ||
        route->source_port == 0 || route->destination_port == 0) return 0;
    struct net_peer staged;
    memset(&staged, 0, sizeof(staged)); staged.route = *route;
    if (!net_session_init(&staged.session, identity, remote, secret) ||
        replica_open(&staged.replica, store, &staged.session) != 1) {
        crypto_wipe(&staged, sizeof(staged)); return 0;
    }
    *peer = staged; peer->replica.session = &peer->session;
    crypto_wipe(&staged, sizeof(staged)); return 1;
}
int net_peer_extension(struct net_peer *peer, void *context, net_peer_receiver receive, net_peer_sender send) {
    if (peer == NULL || context == NULL || receive == NULL || send == NULL || peer->failed) return 0;
    peer->extension=context; peer->receive_extension=receive; peer->send_extension=send; return 1;
}
static int validate_packet(struct net_peer *peer, const uint8_t *frame, size_t length,
                           const struct jani_udp_datagram *expected) {
    if (peer->validate_packet == NULL) return 1;
    struct jani_udp_datagram actual;
    return peer->validate_packet(frame, length, &actual) &&
        net_bytes_compare(actual.source, expected->source, 4) == 0 &&
        net_bytes_compare(actual.destination, expected->destination, 4) == 0 &&
        actual.source_port == expected->source_port && actual.destination_port == expected->destination_port &&
        actual.payload_offset == expected->payload_offset && actual.payload_length == expected->payload_length;
}
static int flush_pending(struct net_peer *peer) {
    if (peer->pending_length == 0) return 1;
    enum virtio_net_send_result result =
        net_send_udp(&peer->route, peer->pending, peer->pending_length);
    if (result < 0) return 0;
    if (result == VIRTIO_NET_SEND_SUBMITTED) peer->pending_length = 0;
    return 1;
}
int net_peer_step(struct net_peer *peer, uint64_t ticks) {
    if (peer == NULL || peer->failed) return -1;
    if (!virtio_net_is_ready() || object_store_requires_recovery(peer->replica.store) ||
        virtio_net_poll_transmit() == VIRTIO_NET_TRANSMIT_FAILED || !flush_pending(peer)) {
        peer->failed = 1; return -1;
    }
    /* Never consume a request requiring a response while our sole buffer is busy. */
    if (peer->pending_length == 0) {
        struct jani_udp_datagram datagram;
        enum virtio_net_receive_result result = virtio_net_poll(&datagram);
        if (result == VIRTIO_NET_RECEIVE_FAILED) { peer->failed = 1; return -1; }
        if (result == VIRTIO_NET_RECEIVE_PACKET) {
            const uint8_t *frame = virtio_net_received_frame();
            size_t length = virtio_net_received_frame_length();
            if (frame != NULL && length >= 14 && validate_packet(peer, frame, length, &datagram) &&
                datagram.payload_offset <= length &&
                datagram.payload_length <= length - datagram.payload_offset &&
                datagram.payload_length <= NET_PACKET_MAX &&
                net_bytes_compare(frame, peer->route.source_mac, 6) == 0 &&
                net_bytes_compare(frame + 6, peer->route.destination_mac, 6) == 0 &&
                net_bytes_compare(datagram.source, peer->route.destination_ip, 4) == 0 &&
                net_bytes_compare(datagram.destination, peer->route.source_ip, 4) == 0 &&
                datagram.source_port == peer->route.destination_port &&
                datagram.destination_port == peer->route.source_port) {
                uint8_t packet[NET_PACKET_MAX];
                memcpy(packet, frame + datagram.payload_offset, datagram.payload_length);
                if (datagram.payload_length == NET_HELLO_BYTES &&
                    net_bytes_compare(packet, "JN5H", 4) == 0) {
                    (void)net_session_accept(&peer->session, packet, datagram.payload_length);
                } else if (peer->session.active) {
                    int received;
                    if (datagram.payload_length >= 6 && packet[5] >= NET_MESSAGE_MIGRATION &&
                        packet[5] <= NET_MESSAGE_ACK && peer->receive_extension != NULL)
                        received = peer->receive_extension(peer->extension, packet, datagram.payload_length,
                            peer->pending, sizeof(peer->pending), &peer->pending_length);
                    else received = replica_receive(&peer->replica, packet,
                        datagram.payload_length, peer->pending, sizeof(peer->pending),
                        &peer->pending_length);
                    if (received == -1) peer->failed = 1;
                }
            }
            if (!virtio_net_release_receive()) peer->failed = 1;
        }
    }
    if (peer->failed) return -1;
    if (peer->pending_length == 0 && peer->session.receive_sequence == 0 && ticks >= peer->hello_tick) {
        memcpy(peer->pending, peer->session.local_hello, NET_HELLO_BYTES);
        peer->pending_length = NET_HELLO_BYTES;
        peer->hello_tick = ticks > UINT64_MAX - 100 ? UINT64_MAX : ticks + 100;
    } else if (peer->pending_length == 0 && peer->session.active &&
               ticks >= peer->summary_tick) {
        if (peer->send_extension != NULL)
            peer->pending_length = peer->send_extension(peer->extension, peer->pending, sizeof(peer->pending));
        if (peer->pending_length == 0)
            peer->pending_length = replica_next_packet(&peer->replica, peer->pending, sizeof(peer->pending));
        if (peer->pending_length > sizeof(peer->pending)) { peer->failed=1; return -1; }
        /* Stop-wait transfers must not flood the sole receive slot with signed retries. */
        uint64_t interval = peer->send_extension != NULL ? 100 : 10;
        peer->summary_tick = ticks > UINT64_MAX - interval ? UINT64_MAX : ticks + interval;
    }
    if (!flush_pending(peer)) { peer->failed = 1; return -1; }
    return 1;
}
