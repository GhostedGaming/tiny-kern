#include "logging/print.h"
#include <stddef.h>
#include <multitasking/thread.h>
#include <multitasking/sched.h>

struct blocked_list {
    struct tcb *tcb;
    struct tcb *next;
};

extern void switch_task(struct tcb *t);

struct tcb *current_tcb = NULL;

struct blocked_list *blocked = NULL;

void schedule() {
    if (!thread_list) {
        print("No threads to switch to\n");
        return;
    }

    if (!current_tcb) {
        struct tcb *t = thread_list;
        do {
            if (t->state == Ready) {
                t->state = Running;
                print("Switching to first task %d\n", t->tid);
                switch_task(t);
                return;
            }
            t = t->next;
        } while (t != thread_list);
        return;
    }

    struct tcb *t = current_tcb->next;
    for (uint64_t i = 0; i < thread_count; i++) {
        if (t->state == Ready) {
            current_tcb->state = Ready;
            t->state = Running;
            print("Switching to task %d\n", t->tid);
            switch_task(t);
            return;
        }
        t = t->next;
    }
}

struct tcb *sched_current_thread() {
    if (!current_tcb) {
        print("Couldn't get the current TCB\n");
        return NULL;
    }
    print("Current TCB\nTID: %d\n", current_tcb->tid);
    return current_tcb;
}

struct tcb *block_current() {
    current_tcb->state = Blocked;
    struct tcb *t = current_tcb;
    print("Blocking %d\n", t->tid);
    schedule();
    return t;
}

void unblock(struct tcb *t) {
    t->state = Ready;
    print("Unblocking %d\n", t->tid);
}