#include "check.h"
#include "../../kernel/net/session.h"
#include "../../third_party/monocypher/src/monocypher.h"
#include "../../third_party/monocypher/src/optional/monocypher-ed25519.h"
#include <string.h>

unsigned long checks_passed;

static void hex(uint8_t *out, const char *text, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned int value;
        CHECK(sscanf(text + 2*i, "%2x", &value) == 1);
        out[i] = (uint8_t)value;
    }
}

static void primitives(void) {
    uint8_t seed[32], expected[64], signature[64], secret[64], public[32];
    /* RFC 8032 section 7.1, test 1. */
    hex(seed, "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", 32);
    crypto_ed25519_key_pair(secret, public, seed);
    hex(expected, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", 32);
    CHECK(memcmp(public, expected, 32) == 0);
    crypto_ed25519_sign(signature, secret, NULL, 0);
    hex(expected, "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", 64);
    CHECK(memcmp(signature, expected, 64) == 0);
    CHECK(crypto_ed25519_check(signature, public, NULL, 0) == 0);
    signature[0] ^= 1;
    CHECK(crypto_ed25519_check(signature, public, NULL, 0) != 0);
    /* RFC 7748 section 6.1. */
    uint8_t alice[32], bob_public[32], shared[32];
    hex(alice, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", 32);
    hex(bob_public, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", 32);
    crypto_x25519(shared, alice, bob_public);
    hex(expected, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);
    CHECK(memcmp(shared, expected, 32) == 0);
    /* draft-irtf-cfrg-xchacha-03 A.3.1 published test vector (not an RFC). */
    uint8_t key[32], nonce[24], aad[12], text_bytes[114], cipher[114], wanted[114], tag[16], wanted_tag[16], plain[114];
    hex(key,"808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f",32);
    hex(nonce,"404142434445464748494a4b4c4d4e4f5051525354555657",24);
    hex(aad,"50515253c0c1c2c3c4c5c6c7",12);
    hex(text_bytes,"4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e",114);
    hex(wanted,"bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b4522f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff921f9664c97637da9768812f615c68b13b52e",114);
    hex(wanted_tag,"c0875924c1c7987947deafd8780acf49",16);
    crypto_aead_lock(cipher,tag,key,nonce,aad,sizeof(aad),text_bytes,sizeof(text_bytes));
    CHECK(memcmp(cipher,wanted,sizeof(cipher))==0 && memcmp(tag,wanted_tag,sizeof(tag))==0);
    CHECK(crypto_aead_unlock(plain,tag,key,nonce,aad,sizeof(aad),cipher,sizeof(cipher))==0);
    CHECK(memcmp(plain,text_bytes,sizeof(plain))==0);
    crypto_wipe(secret, sizeof(secret));
}

int main(void) {
    primitives();
    struct net_identity a, b, outsider;
    struct net_session sa, sb, wrong, next;
    uint8_t seed[32] = {1}, ephemeral_a[32] = {10}, ephemeral_b[32] = {20};
    CHECK(net_identity_from_seed(&a, seed));
    seed[0] = 2; CHECK(net_identity_from_seed(&b, seed));
    seed[0] = 3; CHECK(net_identity_from_seed(&outsider, seed));
    CHECK(net_session_init(&sa, &a, b.public_key, ephemeral_a));
    CHECK(net_session_init(&sb, &b, a.public_key, ephemeral_b));
    CHECK(!net_session_init(&wrong, &a, a.public_key, ephemeral_a));
    CHECK(net_session_init(&wrong, &outsider, a.public_key, ephemeral_b));
    uint8_t mutated[NET_PACKET_MAX], packet[NET_PACKET_MAX], output[NET_BODY_MAX];
    uint32_t kind = 99; size_t output_length = 99;
    memset(output, 0xa5, sizeof(output));
    CHECK(!net_session_accept(&sa, wrong.local_hello, NET_HELLO_BYTES));
    for (size_t i = 0; i < NET_HELLO_BYTES; i++) {
        memcpy(mutated, sb.local_hello, NET_HELLO_BYTES); mutated[i] ^= 1;
        CHECK(!net_session_accept(&sa, mutated, NET_HELLO_BYTES));
        CHECK(sa.active == 0);
    }
    memcpy(mutated, sb.local_hello, NET_HELLO_BYTES);
    memset(mutated + 40, 0, 32);
    crypto_ed25519_sign(mutated + 72, b.secret_key, mutated, 72);
    CHECK(!net_session_accept(&sa, mutated, NET_HELLO_BYTES));
    CHECK(sa.active == 0);
    CHECK(net_session_accept(&sa, sb.local_hello, NET_HELLO_BYTES));
    CHECK(net_session_accept(&sb, sa.local_hello, NET_HELLO_BYTES));
    CHECK(memcmp(sa.send_key, sb.receive_key, 32) == 0);
    CHECK(memcmp(sa.receive_key, sb.send_key, 32) == 0);
    CHECK(memcmp(sa.send_key, sa.receive_key, 32) != 0);
    CHECK(memcmp(sa.session_id, sb.session_id, 32) == 0);
    CHECK(net_session_accept(&sa, sb.local_hello, NET_HELLO_BYTES));
    ephemeral_b[1] = 1;
    CHECK(net_session_init(&next, &b, a.public_key, ephemeral_b));
    CHECK(!net_session_accept(&sa, next.local_hello, NET_HELLO_BYTES));
    size_t length = net_session_seal(&sa, NET_MESSAGE_UPDATE, (const uint8_t *)"hello", 5, packet, sizeof(packet));
    CHECK(length == 141 && sa.send_sequence == 1);
    for (size_t i = 0; i < length; i++) {
        memcpy(mutated, packet, length); mutated[i] ^= 1;
        CHECK(!net_session_open(&sb, mutated, length, output, sizeof(output), &kind, &output_length));
        CHECK(sb.receive_sequence == 0 && kind == 99 && output_length == 99 && output[0] == 0xa5);
    }
    for (size_t i = 0; i < length; i++)
        CHECK(!net_session_open(&sb, packet, i, output, sizeof(output), &kind, &output_length));
    CHECK(!net_session_open(&sa, packet, length, output, sizeof(output), &kind, &output_length));
    CHECK(!net_session_open(&sb, packet, length, output, 4, &kind, &output_length));
    memcpy(mutated, packet, length); mutated[NET_PACKET_HEADER + 5] ^= 1;
    crypto_ed25519_sign(mutated + length - 64, a.secret_key, mutated, length - 64);
    CHECK(!net_session_open(&sb, mutated, length, output, sizeof(output), &kind, &output_length));
    CHECK(sb.receive_sequence == 0 && output[0] == 0xa5 && kind == 99 && output_length == 99);
    CHECK(net_session_open(&sb, packet, length, output, sizeof(output), &kind, &output_length));
    CHECK(kind == NET_MESSAGE_UPDATE && output_length == 5 && memcmp(output, "hello", 5) == 0);
    CHECK(!net_session_open(&sb, packet, length, output, sizeof(output), &kind, &output_length));
    CHECK(!net_session_seal(&sa, 0, NULL, 0, packet, sizeof(packet)));
    CHECK(!net_session_seal(&sa, 1, NULL, 1, packet, sizeof(packet)));
    CHECK(!net_session_seal(&sa, 1, output, NET_BODY_MAX + 1, packet, sizeof(packet)));
    CHECK(!net_session_seal(&sa, 1, output, NET_BODY_MAX, packet, NET_PACKET_MAX - 1));
    CHECK(sa.send_sequence == 1);
    memset(output, 0x42, sizeof(output));
    length = net_session_seal(&sa, 1, output, sizeof(output), packet, sizeof(packet));
    CHECK(length == NET_PACKET_MAX);
    CHECK(net_session_open(&sb, packet, length, output, sizeof(output), &kind, &output_length));
    CHECK(output_length == NET_BODY_MAX && sb.receive_sequence == 2);
    length = net_session_seal(&sb, NET_MESSAGE_ACK, NULL, 0, packet, sizeof(packet));
    CHECK(net_session_open(&sa, packet, length, output, sizeof(output), &kind, &output_length));
    CHECK(output_length == 0);
    sa.send_sequence = UINT64_MAX;
    CHECK(!net_session_seal(&sa, 1, NULL, 0, packet, sizeof(packet)));
    net_session_destroy(&sa);
    struct net_session wiped = {0};
    CHECK(memcmp(&sa, &wiped, sizeof(sa)) == 0);
    net_session_destroy(&sb); net_session_destroy(&wrong); net_session_destroy(&next);
    printf("test_net_session: %lu checks passed\n", checks_passed);
    return 0;
}
