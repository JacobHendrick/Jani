#include "check.h"
#include "../../kernel/net/crdt.h"
#include <string.h>

unsigned long checks_passed;

static void equal(const struct net_crdt *a, const struct net_crdt *b) {
    CHECK(memcmp(a, b, sizeof(*a)) == 0);
}
static int contains(const struct net_crdt *s, uint64_t value) {
    for (uint32_t i = 0; i < s->count; i++)
        if (s->dots[i].value == value && !s->dots[i].removed) return 1;
    return 0;
}
static void laws(const struct net_crdt *a, const struct net_crdt *b,
                  const struct net_crdt *c) {
    struct net_crdt ab, ba, abc, bc, other, same;
    CHECK(net_crdt_merge(&ab, a, b));
    CHECK(net_crdt_merge(&ba, b, a)); equal(&ab, &ba);
    CHECK(net_crdt_merge(&same, a, a)); equal(&same, a);
    CHECK(net_crdt_merge(&abc, &ab, c));
    CHECK(net_crdt_merge(&bc, b, c));
    CHECK(net_crdt_merge(&other, a, &bc)); equal(&abc, &other);
    CHECK(net_crdt_merge(&ab, &ab, c)); equal(&ab, &abc);
    uint8_t bytes[NET_CRDT_BYTES];
    CHECK(net_crdt_encode(bytes, sizeof(bytes), &abc) == sizeof(bytes));
    CHECK(net_crdt_decode(bytes, sizeof(bytes), &same)); equal(&same, &abc);
}

int main(void) {
    struct net_crdt a, b, c, result, before;
    CHECK(net_crdt_init(&a, NET_CRDT_LWW));
    CHECK(net_crdt_init(&b, NET_CRDT_LWW));
    CHECK(net_crdt_set(&a, 0, (const uint8_t *)"first", 5));
    c = a;
    CHECK(net_crdt_set(&a, 0, (const uint8_t *)"later", 5));
    CHECK(net_crdt_set(&b, 1, (const uint8_t *)"other", 5));
    laws(&a, &b, &c);
    CHECK(net_crdt_merge(&result, &a, &b));
    CHECK(memcmp(result.value, "later", 5) == 0);
    CHECK(net_crdt_set(&b, 1, (const uint8_t *)"wins", 4));
    CHECK(net_crdt_merge(&result, &a, &b));
    CHECK(result.author == 1 && memcmp(result.value, "wins", 4) == 0);
    c = b; c.value[0] ^= 1; before = result;
    CHECK(!net_crdt_merge(&result, &b, &c)); equal(&result, &before);
    CHECK(!net_crdt_set(&result, 2, NULL, 0)); equal(&result, &before);

    CHECK(net_crdt_init(&a, NET_CRDT_ORSET)); b = a;
    CHECK(net_crdt_add(&a, 0, 7)); c = a;
    CHECK(net_crdt_add(&b, 1, 7));
    CHECK(net_crdt_remove(&a, 0, 7));
    CHECK(net_crdt_merge(&result, &a, &b));
    CHECK(contains(&result, 7));
    CHECK(net_crdt_remove(&result, 0, 7)); CHECK(!contains(&result, 7));
    CHECK(net_crdt_merge(&result, &result, &c)); CHECK(!contains(&result, 7));
    laws(&a, &b, &c);

    for (uint64_t round = 0; round < 128; round++) {
        CHECK(net_crdt_init(&a, NET_CRDT_ORSET)); b = a;
        for (uint64_t i = 0; i < 4; i++) {
            CHECK(net_crdt_add(&a, 0, (round + i) % 5));
            CHECK(net_crdt_add(&b, 1, (round * 3 + i) % 5));
        }
        c = a;
        CHECK(net_crdt_remove(&a, 0, round % 5));
        CHECK(net_crdt_remove(&b, 1, (round + 1) % 5));
        laws(&a, &b, &c);
    }
    CHECK(net_crdt_init(&a, NET_CRDT_ORSET)); b = a;
    for (uint64_t i = 0; i < 8; i++) {
        CHECK(net_crdt_add(&a, 0, i)); CHECK(net_crdt_add(&b, 1, i+8));
    }
    CHECK(net_crdt_merge(&result, &a, &b)); before = result;
    CHECK(result.count == 16);
    CHECK(!net_crdt_add(&result, 0, 20)); equal(&result, &before);
    CHECK(net_crdt_remove(&a, 0, 0));
    CHECK(net_crdt_add(&a, 0, 100));
    CHECK(!net_crdt_merge(&result, &a, &b)); equal(&result, &before);
    uint8_t bytes[NET_CRDT_BYTES + 2], copy[NET_CRDT_BYTES + 2];
    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(net_crdt_encode(bytes + 1, NET_CRDT_BYTES, &before) == NET_CRDT_BYTES);
    CHECK(bytes[0] == 0xa5 && bytes[NET_CRDT_BYTES+1] == 0xa5);
    CHECK(net_crdt_decode(bytes + 1, NET_CRDT_BYTES, &result)); equal(&result, &before);
    for (size_t i = 0; i < NET_CRDT_BYTES; i++)
        CHECK(!net_crdt_decode(bytes + 1, i, &result));
    equal(&result, &before);
    bytes[5] = 2; /* invalid format version */
    CHECK(!net_crdt_decode(bytes + 1, NET_CRDT_BYTES, &result)); equal(&result, &before);
    before.count = 17; memcpy(copy, bytes, sizeof(copy));
    CHECK(!net_crdt_encode(bytes + 1, NET_CRDT_BYTES, &before));
    CHECK(memcmp(bytes, copy, sizeof(copy)) == 0);
    CHECK(net_crdt_init(&a, NET_CRDT_ORSET));
    a.vector[0] = UINT64_MAX; before = a;
    CHECK(!net_crdt_add(&a, 0, 1)); equal(&a, &before);
    CHECK(net_crdt_init(&a, NET_CRDT_LWW));
    a.timestamp = UINT64_MAX; a.vector[0] = 1; before = a;
    CHECK(!net_crdt_set(&a, 0, NULL, 0)); equal(&a, &before);
    CHECK(net_crdt_init(&a, NET_CRDT_LWW));
    CHECK(net_crdt_set(&a, 0, (const uint8_t *)"clock", 5));
    CHECK(net_crdt_encode(bytes, NET_CRDT_BYTES, &a) == NET_CRDT_BYTES);
    bytes[16] = 2; /* a vector cannot outrun its Lamport timestamp */
    before = a;
    CHECK(!net_crdt_decode(bytes, NET_CRDT_BYTES, &a)); equal(&a, &before);
    printf("test_crdt: %lu checks passed\n", checks_passed);
    return 0;
}
