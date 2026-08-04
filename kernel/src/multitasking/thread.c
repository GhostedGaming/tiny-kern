#include <stdint.h>
#include <stddef.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/hhdm.h>
#include <mm/heap.h>
#include <logging/print.h>
#include <multitasking/proc.h>
#include <multitasking/thread.h>

#define KSTACK_SIZE 0x10000

struct tcb *thread_list = NULL;

uint64_t thread_count = 0;

struct tcb *create_thread(void *entry, struct pcb *p, void *ustack) {
    struct tcb *t = (struct tcb *)kmalloc(sizeof(struct tcb));
    uint8_t *kstack = kmalloc(KSTACK_SIZE);

    if (!t || !kstack || !ustack) {
        return NULL;
    }

    uintptr_t *sp = (uintptr_t *)(kstack + KSTACK_SIZE);

    *--sp = (uintptr_t)entry;
    *--sp = 0x202;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    t->tid = thread_count++;
    t->ksp = sp;
    t->kstack_top = kstack + KSTACK_SIZE;
    t->tsp = ustack;
    t->addr_space = p->addr_space;
    t->parent = p;
    t->state = Ready;

    if (thread_list == NULL) {
        thread_list = t;
        t->next = t;
    } else {
        t->next = thread_list->next;
        thread_list->next = t;
    }

    if (p->t == NULL) {
        p->t = t;
        t->proc_next = t;
    } else {
        t->proc_next = p->t->proc_next;
        p->t->proc_next = t;
    }

    p->t_count++;

    return t;
}