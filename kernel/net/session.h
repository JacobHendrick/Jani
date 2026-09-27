#ifndef JANI_KERNEL_NET_SESSION_H
#define JANI_KERNEL_NET_SESSION_H

#include <stddef.h>
#include <stdint.h>

#define NET_HELLO_BYTES 136u
#define NET_PACKET_HEADER 56u
#define NET_BODY_MAX 1024u
#define NET_PACKET_MAX (NET_PACKET_HEADER + NET_BODY_MAX + 80u)

enum net_message_kind {
    NET_MESSAGE_SUMMARY = 1, NET_MESSAGE_UPDATE, NET_MESSAGE_GRANT,
    NET_MESSAGE_MIGRATION, NET_MESSAGE_ACK
};

struct net_wire_view {
    uint32_t kind;
    uint32_t reserved;
    uint64_t sequence;
    size_t payload_length;
    size_t signed_length;
};

struct net_identity {
    uint8_t public_key[32];
    uint8_t secret_key[64];
    uint8_t node_id[32];
};

struct net_session {
    const struct net_identity *identity;
    uint8_t pinned_peer[32];
    uint8_t exchange_secret[32];
    uint8_t local_hello[NET_HELLO_BYTES];
    uint8_t remote_hello[NET_HELLO_BYTES];
    uint8_t send_key[32];
    uint8_t receive_key[32];
    uint8_t session_id[32];
    uint64_t send_sequence;
    uint64_t receive_sequence;
    uint32_t active;
};

/* Structural validation only; out is unchanged on rejection. */
int net_wire_validate(const uint8_t *bytes, size_t length,
                       struct net_wire_view *out);

/* Seeds must come from trusted entropy. No function invents entropy. */
int net_identity_from_seed(struct net_identity *out, const uint8_t seed[32]);
int net_session_init(struct net_session *session, const struct net_identity *identity,
                      const uint8_t peer_key[32], const uint8_t fresh_secret[32]);
/* Repeated identical hello is idempotent. Rekey requires a fresh init; an
 * established context never resets counters under an old exchange key. */
int net_session_accept(struct net_session *session, const uint8_t *hello, size_t length);
/* Buffers are valid stable kernel memory, and may not alias session/identity.
 * Failure leaves caller output and counters unchanged. */
size_t net_session_seal(struct net_session *session, uint32_t kind,
                        const uint8_t *body, size_t length,
                        uint8_t *output, size_t capacity);
int net_session_open(struct net_session *session, const uint8_t *packet, size_t length,
                      uint8_t *output, size_t capacity,
                      uint32_t *kind_out, size_t *length_out);
void net_session_destroy(struct net_session *session);

#endif
