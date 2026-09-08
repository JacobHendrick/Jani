#ifndef JANI_KERNEL_SCHED_SCHEDULER_H
#define JANI_KERNEL_SCHED_SCHEDULER_H

#include "../cap/domain.h"

#define SCHED_TRACE_MAX 64u
#define SCHED_TRACE_BYTES (24u + SCHED_TRACE_MAX * 40u)

enum trace_kind { TRACE_RUN = 1, TRACE_WAIT, TRACE_ALLOCATE, TRACE_FAIL, TRACE_SWAP };
struct trace_event {
    struct object_id component;
    uint64_t time;
    uint64_t value;
    uint32_t kind;
    uint32_t reserved;
};
struct scheduler_policy {
    uint32_t interactive;
    uint32_t priority;
    uint64_t budget_cycles;
};
struct scheduler {
    struct capability_domain *domain;
    struct scheduler_policy policies[COMPONENT_MAX];
    uint64_t last_run[COMPONENT_MAX];
    uint64_t used[COMPONENT_MAX];
    uint64_t frame_start;
    uint64_t frame_ticks;
    uint64_t frame_cycles;
    uint64_t consumed;
    uint32_t cursor;
    uint32_t running;
    uint32_t waiting[COMPONENT_MAX];
    uint32_t trace_count;
    uint32_t trace_next;
    struct trace_event trace[SCHED_TRACE_MAX];
    uint64_t (*clock)(void);
};

int scheduler_init(struct scheduler *scheduler, struct capability_domain *domain,
                    uint64_t frame_ticks, uint64_t frame_cycles, uint64_t (*clock)(void));
int scheduler_configure(struct scheduler *scheduler, uint32_t index,
                         struct scheduler_policy policy);
/* Returns 1 for a committed handler, 0 for idle, -1 for a failed handler. */
int scheduler_step(struct scheduler *scheduler, uint64_t now);
void scheduler_trace(struct scheduler *scheduler, struct component *component,
                      uint32_t kind, uint64_t value);
int scheduler_trace_query(const struct scheduler *scheduler, struct object_id owner,
                          uint32_t age, struct trace_event *event);
void scheduler_set_current(struct scheduler *scheduler);
struct scheduler *scheduler_current(void);
int scheduler_trace_validate(const uint8_t *bytes, size_t length);

#endif
