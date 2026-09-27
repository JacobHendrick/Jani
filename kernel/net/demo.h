#ifndef JANI_KERNEL_NET_DEMO_H
#define JANI_KERNEL_NET_DEMO_H
#include "config.h"
#include "../sched/scheduler.h"
int phase5_network_demo(struct object_store *store, const struct net_boot_config *config,
    struct component *component, struct component_set *components, struct scheduler *scheduler);
#endif
