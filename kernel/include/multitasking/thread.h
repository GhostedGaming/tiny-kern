#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <multitasking/proc.h>

extern uint64_t thread_count;

typedef enum {
    Ready,
    Running,
    Blocked,
    Exited,
} state_t;

struct tcb {
    uint64_t tid;
    void *ksp;
    void *kstack_top;
    void *tsp;
    uintptr_t addr_space;
    struct tcb *next;
    struct tcb *proc_next;
    struct pcb *parent;
    uint8_t state;
} __attribute__((packed));

extern struct tcb *thread_list;

struct tcb *create_thread(void *entry, struct pcb *p, void *ustack);
void destroy_thread(struct tcb *t);