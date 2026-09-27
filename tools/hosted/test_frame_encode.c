#include "check.h"
#include "../../kernel/net/transmit.h"
#include <string.h>
#include <stdint.h>

unsigned long checks_passed;
static unsigned int sends;
static uint8_t sent[1514];
static size_t sent_length;
static enum virtio_net_send_result send_result = VIRTIO_NET_SEND_SUBMITTED;

enum virtio_net_send_result virtio_net_send(const uint8_t *frame, size_t length) {
    sends++;
    CHECK(length <= sizeof(sent));
    memcpy(sent, frame, length);
    sent_length = length;
    return send_result;
}

int main(void) {
    struct jani_udp_frame_fields fields = {
        .destination_mac = {2,0,0,0,0,2}, .source_mac = {2,0,0,0,0,1},
        .source_ip = {10,0,0,1}, .destination_ip = {10,0,0,2},
        .source_port = 5555, .destination_port = 5555, .ttl = 64
    };
    uint8_t guarded[1516], payload[1473], before[1516];
    memset(guarded, 0xa5, sizeof(guarded));
    memset(payload, 0x42, sizeof(payload));
    CHECK(sizeof(fields) == 28 && offsetof(struct jani_udp_frame_fields, ttl) == 26);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, payload, 3) == 60);
    CHECK(guarded[0] == 0xa5 && guarded[1515] == 0xa5);
    struct jani_udp_datagram decoded;
    CHECK(jani_udp_frame_decode(guarded + 1, 60, &decoded));
    CHECK(decoded.payload_length == 3 && decoded.source_port == 5555);
    CHECK(memcmp(guarded + 1 + decoded.payload_offset, payload, 3) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, payload, 1472) == 1514);
    CHECK(jani_udp_frame_decode(guarded + 1, 1514, &decoded));
    CHECK(decoded.payload_length == 1472);
    memcpy(before, guarded, sizeof(before));
    CHECK(jani_udp_frame_encode(NULL, 1514, &fields, payload, 3) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, NULL, payload, 3) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, NULL, 3) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 59, &fields, payload, 0) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, payload, 1473) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, SIZE_MAX, &fields, payload, SIZE_MAX) == 0);
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, guarded + 2, 3) == 0);
    fields.ttl = 0;
    CHECK(jani_udp_frame_encode(guarded + 1, 1514, &fields, payload, 3) == 0);
    CHECK(memcmp(guarded, before, sizeof(before)) == 0);
    CHECK(net_send_udp(&fields, payload, 3) == VIRTIO_NET_SEND_INVALID && sends == 0);
    fields.ttl = 64;
    CHECK(net_send_udp(&fields, payload, 3) == VIRTIO_NET_SEND_SUBMITTED && sends == 1);
    CHECK(jani_udp_frame_decode(sent, sent_length, &decoded));
    send_result = VIRTIO_NET_SEND_BUSY;
    CHECK(net_send_udp(&fields, NULL, 0) == VIRTIO_NET_SEND_BUSY && sends == 2);
    printf("test_frame_encode: %lu checks passed\n", checks_passed);
    return 0;
}
