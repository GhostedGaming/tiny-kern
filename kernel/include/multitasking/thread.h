#pragma once

#include <stdint.h>
#include <stdbool.h>

extern uint64_t thread_count;

typedef enum {
    Ready,
    Running,
    Blocked,
} state_t;

struct tcb {
    uint64_t tid;
    void *ksp;
    void *kstack_top;
    void *tsp;
    uintptr_t addr_space;
    struct tcb *next;
    uint8_t state;
};

extern struct tcb *thread_list;

struct tcb *create_thread(void *entry);
