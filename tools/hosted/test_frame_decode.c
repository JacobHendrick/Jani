#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/net/frame_decode.h"
#include "check.h"

unsigned long checks_passed;

static const uint8_t valid_frame[] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    0x52, 0x54, 0x00, 0xab, 0xcd, 0xef,
    0x08, 0x00,
    0x45, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x11, 0x6a, 0xbb, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08,
    0x04, 0xd2, 0x16, 0x2e, 0x00, 0x0b, 0x10, 0x62,
    0x61, 0x62, 0x63,
};

static void expect_failure(const uint8_t *frame, size_t length) {
    struct jani_udp_datagram output;
    struct jani_udp_datagram saved;

    memset(&output, 0xa5, sizeof(output));
    saved = output;
    CHECK(jani_udp_frame_decode(frame, length, &output) == 0);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
}

static void test_valid_frame(void) {
    struct {
        uint8_t before;
        struct jani_udp_datagram value;
        uint8_t after;
    } guarded;
    const uint8_t source[] = {1, 2, 3, 4};
    const uint8_t destination[] = {5, 6, 7, 8};

    memset(&guarded, 0xa5, sizeof(guarded));
    CHECK(jani_udp_frame_decode(valid_frame, sizeof(valid_frame),
                                &guarded.value) == 1);
    CHECK(memcmp(guarded.value.source, source, sizeof(source)) == 0);
    CHECK(memcmp(guarded.value.destination, destination,
                 sizeof(destination)) == 0);
    CHECK(guarded.value.source_port == 1234);
    CHECK(guarded.value.destination_port == 5678);
    CHECK(guarded.value.payload_offset == 42);
    CHECK(guarded.value.payload_length == 3);
    CHECK(guarded.value.payload_offset <= sizeof(valid_frame));
    CHECK(guarded.value.payload_length <=
          sizeof(valid_frame) - guarded.value.payload_offset);
    CHECK(memcmp(valid_frame + guarded.value.payload_offset, "abc", 3) == 0);
    CHECK(guarded.before == 0xa5 && guarded.after == 0xa5);
}

static void test_failures(void) {
    uint8_t changed[sizeof(valid_frame)];

    expect_failure(NULL, 0);
    expect_failure(valid_frame, 13);
    CHECK(jani_udp_frame_decode(valid_frame, sizeof(valid_frame), NULL) == 0);

    memcpy(changed, valid_frame, sizeof(changed));
    changed[12] = 0x86;
    changed[13] = 0xdd;
    expect_failure(changed, sizeof(changed));

    memcpy(changed, valid_frame, sizeof(changed));
    changed[sizeof(changed) - 1] ^= 1;
    expect_failure(changed, sizeof(changed));
}

int main(void) {
    test_valid_frame();
    test_failures();
    printf("test_frame_decode: %lu checks passed\n", checks_passed);
    return 0;
}
