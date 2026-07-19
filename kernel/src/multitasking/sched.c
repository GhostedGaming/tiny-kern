#include "logging/print.h"
#include <stddef.h>
#include <multitasking/thread.h>

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
        next = current_tcb->next;
    }

    if (!next) {
        return;
    }
    
    switch_task(next);
}