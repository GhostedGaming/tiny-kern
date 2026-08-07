#include <stddef.h>
#include <logging/print.h>
#include <multitasking/thread.h>
#include <multitasking/sched.h>

struct blocked_list {
    struct tcb *tcb;
    struct tcb *next;
};

extern void switch_task(struct tcb *t);

struct tcb *current_tcb = NULL;

static void reap_exited() {
    if (!thread_list) {
        return;
    }

    struct tcb *r = thread_list;
    do {
        struct tcb *next = r->next;
        if (r->state == Exited && r != current_tcb) {
            struct pcb *parent = r->parent;
            destroy_thread(r);
            if (parent && parent->t_count == 0 && parent->t == NULL) {
                proc_destroy(parent);
            }
            if (!thread_list) {
                return;
            }
            r = next;
        } else {
            r = next;
        }
    } while (r != thread_list);
}

void schedule() {
    reap_exited();

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
            if (current_tcb->state == Running) {
                current_tcb->state = Ready;
            }
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

struct pcb *sched_current_proc() {
    if (!current_tcb || !current_tcb->parent) {
        print("Couldn't get the current PCB\n");
        return NULL;
    }

    return current_tcb->parent;
}

struct tcb *block_current() {
    asm volatile ("cli");
    current_tcb->state = Blocked;
    asm volatile ("sti");

    struct tcb *t = current_tcb;
    print("Blocking %d\n", t->tid);
    schedule();
    return t;
}

void sched_sleep_thread(struct tcb *t) {
    t->state = Sleeping;
    print("Sleeping thread: %d\n", t->tid);
}

void sched_wake_thread(struct tcb *t) {
    t->state = Ready;
    print("Waking thread: %d\n", t->tid);
}

void unblock(struct tcb *t) {
    asm volatile ("cli");
    t->state = Ready;
    asm volatile ("sti");
    print("Unblocking %d\n", t->tid);
}