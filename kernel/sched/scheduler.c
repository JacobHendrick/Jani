#include "scheduler.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../wasm/runtime.h"
#include "../replay/record.h"

static struct scheduler *active_scheduler;
void scheduler_set_current(struct scheduler *scheduler) { active_scheduler = scheduler; }
struct scheduler *scheduler_current(void) { return active_scheduler; }

int scheduler_init(struct scheduler *s, struct capability_domain *domain,
                    uint64_t frame_ticks, uint64_t frame_cycles, uint64_t (*clock)(void)) {
    if (s == NULL || domain == NULL || clock == NULL || frame_ticks == 0 || frame_cycles == 0) return 0;
    memset(s, 0, sizeof(*s));
    s->domain = domain;
    s->frame_ticks = frame_ticks;
    s->frame_cycles = frame_cycles;
    s->clock = clock;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) s->policies[i].budget_cycles = frame_cycles / COMPONENT_MAX;
    const struct object_id trace_id = {6, 0};
    if (object_table_find(&domain->store->table, trace_id) != NULL) {
        struct object_header h;
        const uint8_t *bytes;
        size_t length;
        if (!object_store_get(domain->store, trace_id, &h, &bytes, &length) ||
            !object_id_equal(h.type_id, (struct object_id){0, 9}) ||
            !scheduler_trace_validate(bytes, length)) return 0;
        memcpy(&s->trace_count, bytes + 8, 4);
        memcpy(&s->trace_next, bytes + 12, 4);
        memcpy(s->trace, bytes + 24, sizeof(s->trace));
    }
    return 1;
}

int scheduler_configure(struct scheduler *s, uint32_t index, struct scheduler_policy p) {
    uint64_t reserved = 0;
    if (s == NULL || s->running || index >= COMPONENT_MAX || p.interactive > 1 ||
        p.priority > 7 || p.budget_cycles == 0 || p.budget_cycles > s->frame_cycles) return 0;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        struct scheduler_policy candidate = i == index ? p : s->policies[i];
        if (candidate.interactive) {
            if (candidate.budget_cycles > s->frame_cycles - reserved) return 0;
            reserved += candidate.budget_cycles;
        }
    }
    s->policies[index] = p;
    return 1;
}

static void emit_trace(struct scheduler *s, struct component *c, uint32_t kind, uint64_t time, uint64_t value) {
    if (s == NULL || c == NULL || kind < TRACE_RUN || kind > TRACE_SWAP) return;
    s->trace[s->trace_next] = (struct trace_event){c->root_id, time, value, kind, 0};
    s->trace_next = (s->trace_next + 1) % SCHED_TRACE_MAX;
    if (s->trace_count < SCHED_TRACE_MAX) s->trace_count++;
}

void scheduler_trace(struct scheduler *s, struct component *c, uint32_t kind, uint64_t value) {
    if (c != NULL) emit_trace(s, c, kind, c->logical_time, value);
}

int scheduler_trace_query(const struct scheduler *s, struct object_id owner,
                          uint32_t age, struct trace_event *event) {
    if (s == NULL || event == NULL || age >= SCHED_TRACE_MAX) return 0;
    for (uint32_t i = 0; i < s->trace_count; i++) {
        uint32_t index = (s->trace_next + SCHED_TRACE_MAX - i - 1) % SCHED_TRACE_MAX;
        if (!object_id_equal(owner, s->trace[index].component)) continue;
        if (age-- == 0) { *event = s->trace[index]; return 1; }
    }
    return 0;
}

static int trace_stage(struct scheduler *s) {
    uint8_t bytes[SCHED_TRACE_BYTES] = {0};
    uint64_t magic = UINT64_C(0x4A414E4954524331);
    memcpy(bytes, &magic, 8);
    memcpy(bytes + 8, &s->trace_count, 4);
    memcpy(bytes + 12, &s->trace_next, 4);
    memcpy(bytes + 24, s->trace, sizeof(s->trace));
    uint32_t crc = object_crc32c(bytes + 24, sizeof(s->trace));
    memcpy(bytes + 16, &crc, 4);
    struct object_store_put_request request = {{6, 0}, {0, 9}, {0, 0}, {0, 0},
        s->frame_start, bytes, sizeof(bytes)};
    return capability_domain_write(s->domain, &request);
}

static int runnable(const struct component *c, uint64_t now) {
    return c != NULL && !c->exited &&
        (c->mailbox_used != 0 || (c->timer_armed && c->timer_deadline <= now));
}

static uint64_t add_saturated(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

int scheduler_step(struct scheduler *s, uint64_t now) {
    int selected = -1;
    uint64_t score = 0;
    if (s == NULL || s->running || s->domain->halted || now < s->frame_start) return -1;
    if (now - s->frame_start >= s->frame_ticks) {
        s->frame_start = now;
        s->consumed = 0;
        memset(s->used, 0, sizeof(s->used));
    }
    uint64_t reserved = 0;
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        if (s->policies[i].interactive && s->used[i] < s->policies[i].budget_cycles)
            reserved += s->policies[i].budget_cycles - s->used[i];
    }
    for (uint32_t offset = 0; offset < COMPONENT_MAX; offset++) {
        uint32_t i = (s->cursor + offset) % COMPONENT_MAX;
        struct component *c = s->domain->components->items[i];
        struct scheduler_policy p = s->policies[i];
        if (!runnable(c, now) || s->used[i] >= p.budget_cycles || s->consumed >= s->frame_cycles) continue;
        if (!p.interactive && (reserved >= s->frame_cycles - s->consumed ||
            p.budget_cycles - s->used[i] > s->frame_cycles - s->consumed - reserved)) continue;
        uint64_t age = now >= s->last_run[i] ? now - s->last_run[i] : 0;
        uint64_t candidate = (p.interactive ? UINT64_C(1) << 63 : 0) + p.priority * 8 + (age > 64 ? 64 : age);
        if (selected < 0 || candidate > score) { selected = (int)i; score = candidate; }
    }
    for (uint32_t i = 0; i < COMPONENT_MAX; i++) {
        struct component *c = s->domain->components->items[i];
        if (c == NULL || c->exited) continue;
        uint32_t reason = (int)i == selected ? 0 :
            (!runnable(c, now) ? (c->timer_armed ? 2u : 1u) : 3u);
        if (reason != 0 && reason != s->waiting[i]) emit_trace(s, c, TRACE_WAIT, now, reason);
        s->waiting[i] = reason;
    }
    if (selected < 0) return 0;
    uint32_t index = (uint32_t)selected;
    struct component *c = s->domain->components->items[index];
    uint8_t *memory;
    size_t size;
    if (!jani_wasm_instance_memory(c->instance, &memory, &size)) return -1;
    uint8_t *saved = kmalloc(size == 0 ? 1 : size);
    if (saved == NULL) return -1;
    if (!capability_domain_begin(s->domain)) { kfree(saved); return -1; }
    memcpy(saved, memory, size);
    s->running = 1;
    scheduler_set_current(s);
    c->logical_time = now;
    int timer = c->mailbox_used == 0;
    if (timer) c->timer_armed = 0;
    jani_wasm_set_current_component(c);
    scheduler_trace(s, c, TRACE_RUN, timer ? 1 : 2);
    uint64_t start = s->clock();
    int ok = replay_handler(c, now, (uint32_t)timer) &&
        jani_wasm_instance_call(c->instance, c->exec_env, timer ? "jani_on_timer" : "jani_on_message");
    if (replay_current() != NULL && replay_current()->owner == c && replay_current()->failed) ok = 0;
    uint64_t elapsed = s->clock() - start;
    s->used[index] = add_saturated(s->used[index], elapsed);
    s->consumed = add_saturated(s->consumed, elapsed);
    s->last_run[index] = now;
    s->cursor = (index + 1) % COMPONENT_MAX;
    c->metrics.invocations = add_saturated(c->metrics.invocations, 1);
    c->metrics.cycles = add_saturated(c->metrics.cycles, elapsed);
    struct component_metrics observed = c->metrics;
    if (ok && !c->exited) {
        capability_domain_state_changed(s->domain, c);
        ok = trace_stage(s);
        if (ok) ok = capability_domain_commit(s->domain) == OBJECT_STORE_BATCH_COMMITTED;
    } else ok = 0;
    if (!ok) {
        if (replay_current() != NULL && replay_current()->owner == c) replay_current()->failed = 1;
        capability_domain_abort(s->domain);
        c->metrics = observed;
        uint8_t *current_memory;
        size_t current_size;
        if (jani_wasm_instance_memory(c->instance, &current_memory, &current_size) && current_size >= size) {
            memcpy(current_memory, saved, size);
            memset(current_memory + size, 0, current_size - size);
        }
        /* Restart must recreate the interpreter, including its non-memory state. */
        c->exited = 1;
        c->exit_code = JANI_EINVAL;
        scheduler_trace(s, c, TRACE_FAIL, elapsed);
    }
    kfree(saved);
    s->running = 0;
    jani_wasm_set_current_component(NULL);
    return ok ? 1 : -1;
}
