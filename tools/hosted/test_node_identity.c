#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/net/node_identity.h"
#include "check.h"

unsigned long checks_passed;

_Static_assert(sizeof(struct node_public_key) == NODE_PUBLIC_KEY_SIZE,
               "public key must contain only its bytes");
_Static_assert(_Alignof(struct node_public_key) == 1,
               "C and Zig key alignment must agree");

static void test_equality(void) {
    struct node_public_key left = {{0}};
    struct node_public_key right = left;

    CHECK(node_public_key_equal(left, right) == 1);
    for (size_t i = 0; i < NODE_PUBLIC_KEY_SIZE; i++) {
        right.bytes[i] = 255;
        CHECK(node_public_key_equal(left, right) == 0);
        CHECK(node_public_key_equal(right, left) == 0);
        right.bytes[i] = 0;
    }
}

static void test_decode(void) {
    uint8_t input[NODE_PUBLIC_KEY_SIZE + 1];
    struct {
        uint8_t before;
        struct node_public_key key;
        uint8_t after;
    } output;
    const size_t invalid_lengths[] = {0, 1, NODE_PUBLIC_KEY_SIZE - 1,
                                     NODE_PUBLIC_KEY_SIZE + 1, SIZE_MAX};

    for (size_t i = 0; i < sizeof(input); i++) {
        input[i] = (uint8_t)i;
    }
    memset(&output, 0xa5, sizeof(output));
    CHECK(node_public_key_decode(input, NODE_PUBLIC_KEY_SIZE, &output.key) == 1);
    CHECK(memcmp(output.key.bytes, input, NODE_PUBLIC_KEY_SIZE) == 0);
    CHECK(output.before == 0xa5 && output.after == 0xa5);
    input[0] = 255;
    CHECK(output.key.bytes[0] == 0);

    const struct node_public_key saved = output.key;
    for (size_t i = 0; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); i++) {
        CHECK(node_public_key_decode(input, invalid_lengths[i], &output.key) == 0);
        CHECK(memcmp(&output.key, &saved, sizeof(saved)) == 0);
        CHECK(output.before == 0xa5 && output.after == 0xa5);
    }
    CHECK(node_public_key_decode(NULL, NODE_PUBLIC_KEY_SIZE, &output.key) == 0);
    CHECK(memcmp(&output.key, &saved, sizeof(saved)) == 0);
    CHECK(node_public_key_decode(input, NODE_PUBLIC_KEY_SIZE, NULL) == 0);
    CHECK(node_public_key_decode(NULL, 0, NULL) == 0);

    /* Zero bytes are accepted as a representation, not as an authenticated key. */
    memset(input, 0, sizeof(input));
    CHECK(node_public_key_decode(input, NODE_PUBLIC_KEY_SIZE, &output.key) == 1);
    CHECK(memcmp(output.key.bytes, input, NODE_PUBLIC_KEY_SIZE) == 0);
}

static void test_overlapping_input(void) {
    struct {
        uint8_t before;
        struct node_public_key key;
        uint8_t after;
    } buffer;
    uint8_t *bytes = (uint8_t *)&buffer;
    uint8_t expected[NODE_PUBLIC_KEY_SIZE];

    /* Exercise input one byte before, equal to, and one byte after the output. */
    for (size_t offset = 0; offset < 3; offset++) {
        for (size_t i = 0; i < sizeof(buffer); i++) {
            bytes[i] = (uint8_t)i;
        }
        memcpy(expected, bytes + offset, sizeof(expected));
        CHECK(node_public_key_decode(bytes + offset, sizeof(expected), &buffer.key) == 1);
        CHECK(memcmp(buffer.key.bytes, expected, sizeof(expected)) == 0);
        CHECK(buffer.before == 0 && buffer.after == sizeof(buffer) - 1);
    }
}

int main(void) {
    test_equality();
    test_decode();
    test_overlapping_input();
    printf("test_node_identity: %lu checks passed\n", checks_passed);
    return 0;
}
