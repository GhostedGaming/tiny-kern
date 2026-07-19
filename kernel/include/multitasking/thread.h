#pragma once

#include <stdint.h>

struct tcb {
    void *ksp;
    void *tsp;
    void *addr_space;
    struct tcb *next;
    uint8_t state;
};

extern struct tcb *thread_list;

void schedule();

struct tcb *create_thread(void *entry);