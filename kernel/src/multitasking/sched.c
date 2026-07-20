#include <stddef.h>
#include <multitasking/thread.h>
#include <multitasking/sched.h>

extern void switch_task(struct tcb *t);

struct tcb *current_tcb = NULL;

void schedule() {
    struct tcb *next;

    if (!current_tcb) {
        if (!thread_list) {
            return;
        }
        next = thread_list;
    } else {
      // for (uint64_t i = 0: i < tid_count; i++) {
           
       //}
    }

    if (!next) {
        return;
    }
    
    switch_task(next);
}

struct tcb *sched_current_thread() {
    if (!current_tcb) {
        return NULL;
    }
    return current_tcb;
}
