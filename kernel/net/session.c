#include "session.h"
#include "../lib/string.h"
#include "../../third_party/monocypher/src/monocypher.h"
#include "../../third_party/monocypher/src/optional/monocypher-ed25519.h"

static int public_key_less(const uint8_t *left, const uint8_t *right) {
    for (size_t i = 0; i < 32; i++) {
        if (left[i] != right[i]) return left[i] < right[i];
    }
    return 0;
}

static void write64(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8; i++) out[i] = (uint8_t)(value >> (8u * i));
}

int net_identity_from_seed(struct net_identity *out, const uint8_t seed[32]) {
    if (out == NULL || seed == NULL) return 0;
    uint8_t copy[32], named[40];
    memcpy(copy, seed, sizeof(copy));
    struct net_identity next;
    crypto_ed25519_key_pair(next.secret_key, next.public_key, copy);
    memcpy(named, "JANI5-ID", 8);
    memcpy(named + 8, next.public_key, 32);
    crypto_blake2b(next.node_id, 32, named, sizeof(named));
    memcpy(out, &next, sizeof(next));
    crypto_wipe(&next, sizeof(next));
    return 1;
}

int net_session_init(struct net_session *s, const struct net_identity *identity,
                      const uint8_t peer_key[32], const uint8_t fresh_secret[32]) {
    static const uint8_t zero[32];
    if (s == NULL || identity == NULL || peer_key == NULL || fresh_secret == NULL ||
        crypto_verify32(identity->public_key, peer_key) == 0 ||
        crypto_verify32(fresh_secret, zero) == 0) return 0;
    struct net_session next;
    memset(&next, 0, sizeof(next));
    next.identity = identity;
    memcpy(next.pinned_peer, peer_key, 32);
    memcpy(next.exchange_secret, fresh_secret, 32);
    memcpy(next.local_hello, "JN5H", 4);
    next.local_hello[4] = 1;
    memcpy(next.local_hello + 8, identity->public_key, 32);
    crypto_x25519_public_key(next.local_hello + 40, fresh_secret);
    crypto_ed25519_sign(next.local_hello + 72, identity->secret_key,
                        next.local_hello, 72);
    memcpy(s, &next, sizeof(next));
    crypto_wipe(&next, sizeof(next));
    return 1;
}

int net_session_accept(struct net_session *s, const uint8_t *hello, size_t length) {
    uint8_t copy[NET_HELLO_BYTES], shared[32], transcript[184], keys[64];
    static const uint8_t zero[32];
    struct net_wire_view view;
    if (s == NULL || s->identity == NULL || hello == NULL || length != sizeof(copy)) return 0;
    memcpy(copy, hello, sizeof(copy));
    if (!net_wire_validate(copy, sizeof(copy), &view) || view.kind != 0 ||
        crypto_verify32(copy + 8, s->pinned_peer) != 0 ||
        crypto_ed25519_check(copy + 72, s->pinned_peer, copy, 72) != 0) return 0;
    if (s->active) return crypto_verify64(copy + 8, s->remote_hello + 8) == 0;
    crypto_x25519(shared, s->exchange_secret, copy + 40);
    if (crypto_verify32(shared, zero) == 0) {
        crypto_wipe(shared, sizeof(shared));
        return 0;
    }
    int first = public_key_less(s->identity->public_key, s->pinned_peer);
    memcpy(transcript, "JANI5KDF", 8);
    memcpy(transcript + 8, shared, 32);
    memcpy(transcript + 40, first ? s->local_hello : copy, 72);
    memcpy(transcript + 112, first ? copy : s->local_hello, 72);
    crypto_blake2b(keys, 64, transcript, sizeof(transcript));
    memcpy(s->send_key, keys + (first ? 0 : 32), 32);
    memcpy(s->receive_key, keys + (first ? 32 : 0), 32);
    memcpy(transcript, "JANI5SID", 8);
    crypto_blake2b(s->session_id, 32, transcript, sizeof(transcript));
    memcpy(s->remote_hello, copy, sizeof(copy));
    s->active = 1;
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(keys, sizeof(keys));
    crypto_wipe(transcript, sizeof(transcript));
    crypto_wipe(s->exchange_secret, sizeof(s->exchange_secret));
    return 1;
}

size_t net_session_seal(struct net_session *s, uint32_t kind,
                        const uint8_t *body, size_t length,
                        uint8_t *output, size_t capacity) {
    uint8_t packet[NET_PACKET_MAX], nonce[24] = {0};
    if (s == NULL || !s->active || kind < 1 || kind > NET_MESSAGE_ACK ||
        length > NET_BODY_MAX || (body == NULL && length != 0) ||
        output == NULL || capacity < 136u + length || s->send_sequence == UINT64_MAX) return 0;
    size_t bytes = 136u + length;
    uint64_t sequence = s->send_sequence + 1;
    memset(packet, 0, NET_PACKET_HEADER);
    memcpy(packet, "JN5D", 4);
    packet[4] = 1;
    packet[5] = (uint8_t)kind;
    memcpy(packet + 8, s->session_id, 32);
    write64(packet + 40, sequence);
    packet[48] = (uint8_t)length;
    packet[49] = (uint8_t)(length >> 8);
    write64(nonce + 16, sequence);
    crypto_aead_lock(packet + NET_PACKET_HEADER, packet + NET_PACKET_HEADER + length,
                     s->send_key, nonce, packet, NET_PACKET_HEADER, body, length);
    crypto_ed25519_sign(packet + bytes - 64, s->identity->secret_key, packet, bytes - 64);
    memcpy(output, packet, bytes);
    s->send_sequence = sequence;
    crypto_wipe(packet, sizeof(packet));
    return bytes;
}

int net_session_open(struct net_session *s, const uint8_t *packet, size_t length,
                      uint8_t *output, size_t capacity,
                      uint32_t *kind_out, size_t *length_out) {
    uint8_t copy[NET_PACKET_MAX], plain[NET_BODY_MAX], nonce[24] = {0};
    struct net_wire_view view;
    if (s == NULL || !s->active || packet == NULL || output == NULL ||
        kind_out == NULL || length_out == NULL || length > sizeof(copy)) return 0;
    memcpy(copy, packet, length);
    if (!net_wire_validate(copy, length, &view) || view.kind == 0 ||
        view.payload_length > capacity || view.sequence <= s->receive_sequence ||
        crypto_verify32(copy + 8, s->session_id) != 0 ||
        crypto_ed25519_check(copy + view.signed_length, s->pinned_peer, copy, view.signed_length) != 0) return 0;
    write64(nonce + 16, view.sequence);
    if (crypto_aead_unlock(plain, copy + NET_PACKET_HEADER + view.payload_length,
                          s->receive_key, nonce, copy, NET_PACKET_HEADER,
                          copy + NET_PACKET_HEADER, view.payload_length) != 0) {
        crypto_wipe(plain, sizeof(plain)); return 0;
    }
    memcpy(output, plain, view.payload_length);
    *kind_out = view.kind;
    *length_out = view.payload_length;
    s->receive_sequence = view.sequence;
    crypto_wipe(plain, sizeof(plain));
    return 1;
}

void net_session_destroy(struct net_session *session) {
    if (session != NULL) crypto_wipe(session, sizeof(*session));
}
