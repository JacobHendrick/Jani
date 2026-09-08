#ifndef JANI_KERNEL_REPLAY_RECORD_H
#define JANI_KERNEL_REPLAY_RECORD_H

#include "../wasm/component.h"

#define REPLAY_BYTES 65536u
#define REPLAY_ARGUMENTS 8u
#define REPLAY_IO_MAX 8192u
enum replay_mode { REPLAY_OFF, REPLAY_RECORD, REPLAY_PLAY };
struct replay_record {
    uint32_t kind;
    uint32_t reserved;
    uint64_t sequence;
    uint64_t arguments[REPLAY_ARGUMENTS];
    int64_t result;
    uint32_t input_size;
    uint32_t output_size;
};
_Static_assert(sizeof(struct replay_record) == 96, "replay event layout");
struct replay_session {
    enum replay_mode mode;
    struct component *owner;
    uint8_t bytes[REPLAY_BYTES];
    size_t used;
    size_t cursor;
    uint64_t sequence;
    uint32_t failed;
    uint8_t *initial;
    size_t initial_size;
    struct object_id module_id;
    uint8_t *expected;
    size_t expected_size;
};

int replay_start(struct replay_session *session, struct component *owner);
void replay_stop(struct replay_session *session);
void replay_set_current(struct replay_session *session);
struct replay_session *replay_current(void);
/* before: 1 invokes the live syscall, 0 supplies replay output, -1 fails. */
int replay_before(struct replay_session *session, uint32_t kind, const uint64_t arguments[8],
                   const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, int64_t *result);
int replay_after(struct replay_session *session, uint32_t kind, const uint64_t arguments[8],
                  const uint8_t *input, size_t input_size, const uint8_t *output, size_t output_size, int64_t result);
int replay_handler(struct component *owner, uint64_t now, uint32_t timer);
int replay_verify(struct replay_session *session);
int replay_save(struct replay_session *session, struct object_store *store);
int replay_load(struct replay_session *session, struct component *owner);
int replay_log_validate(const uint8_t *bytes, size_t length);

#endif
