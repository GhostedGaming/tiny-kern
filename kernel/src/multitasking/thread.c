#include <stdint.h>
#include <stddef.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/hhdm.h>
#include <logging/print.h>
#include <multitasking/thread.h>

struct tcb *thread_list = NULL;

struct tcb *create_thread(void *entry) {
    struct tcb *t = phys_to_virt(frame_alloc());

    uint8_t *stack = phys_to_virt(frame_alloc());

    uintptr_t *sp = (uintptr_t *)(stack + 4096);

    *--sp = (uintptr_t)entry;
    *--sp = 0x200; // rflags (interrupts enabled)

    *--sp = 0; // rbx
    *--sp = 0; // rbp
    *--sp = 0; // r12
    *--sp = 0; // r13
    *--sp = 0; // r14
    *--sp = 0; // r15

    t->ksp = sp;

    t->tsp = NULL;
    t->addr_space = (void *)paging_create_pml4();
    t->next = t;
    t->state = 0;

    if (thread_list == NULL) {
        thread_list = t;
        t->next = t;
    } else {
        t->next = thread_list->next;
        thread_list->next = t;
    }

    return t;
}