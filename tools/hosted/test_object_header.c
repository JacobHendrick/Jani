#include <stdint.h>
#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_header.h"
#include "check.h"

unsigned long checks_passed;

struct test_object {
    struct object_header header;
    uint8_t payload[5];
};

static void make_valid_object(struct test_object *object) {
    memset(object, 0, sizeof(*object));

    object->header.magic = OBJECT_HEADER_MAGIC;
    object->header.format_version = OBJECT_HEADER_FORMAT_VERSION;
    object->header.id.high = 1;
    object->header.id.low = 2;
    object->header.type_id.high = 3;
    object->header.type_id.low = 4;
    object->header.version = 1;
    object->header.payload_size = sizeof(object->payload);
    object->header.creator_id.high = 5;
    object->header.modifier_id.high = 6;
    object->header.logical_timestamp = 7;

    object->payload[0] = 0x10;
    object->payload[1] = 0x20;
    object->payload[2] = 0x30;
    object->payload[3] = 0x40;
    object->payload[4] = 0x50;
    object->header.payload_crc32c =
        object_crc32c(object->payload, sizeof(object->payload));
}

static void test_valid_header(void) {
    struct test_object object;

    make_valid_object(&object);
    CHECK(object_header_validate(
        (const uint8_t *)&object,
        OBJECT_HEADER_SIZE + sizeof(object.payload)
    ));
}

static void test_invalid_header_fields(void) {
    struct test_object object;

    make_valid_object(&object);
    object.header.magic = 0;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));

    make_valid_object(&object);
    object.header.id.high = 0;
    object.header.id.low = 0;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));

    make_valid_object(&object);
    object.header.type_id.high = 0;
    object.header.type_id.low = 0;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));

    make_valid_object(&object);
    object.header.version = 0;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));
}

static void test_corrupt_or_incomplete_object(void) {
    struct test_object object;

    make_valid_object(&object);
    CHECK(!object_header_validate(NULL, sizeof(object)));
    CHECK(!object_header_validate((const uint8_t *)&object,
                                  OBJECT_HEADER_SIZE - 1));

    object.payload[2] ^= 0xFF;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));

    make_valid_object(&object);
    object.header.payload_size++;
    CHECK(!object_header_validate((const uint8_t *)&object,
                                  OBJECT_HEADER_SIZE + sizeof(object.payload)));

    make_valid_object(&object);
    object.header.flags = 1;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));

    make_valid_object(&object);
    object.header._reserved = 1;
    CHECK(!object_header_validate((const uint8_t *)&object, sizeof(object)));
}

int main(void) {
    test_valid_header();
    test_invalid_header_fields();
    test_corrupt_or_incomplete_object();

    printf("test_object_header: %lu checks passed\n", checks_passed);
    return 0;
}
