#include <stdint.h>
#include <stddef.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/hhdm.h>
#include <logging/print.h>
#include <multitasking/thread.h>

struct tcb *thread_list = NULL;

uint64_t tid = 0;

struct tcb *create_thread(void *entry) {
    struct tcb *t = phys_to_virt(frame_alloc());
    uint8_t *kstack = phys_to_virt(frame_alloc());

    uintptr_t *sp = (uintptr_t *)(kstack + 4096);

    *--sp = (uintptr_t)entry;   // return address for switch_task's `ret`
    *--sp = 0x202;              // rflags (IF=1)
    *--sp = 0;                  // rax
    *--sp = 0;                  // rbx
    *--sp = 0;                  // rcx
    *--sp = 0;                  // rdx
    *--sp = 0;                  // rsi
    *--sp = 0;                  // rbp
    *--sp = 0;                  // r8
    *--sp = 0;                  // r9
    *--sp = 0;                  // r10
    *--sp = 0;                  // r11
    *--sp = 0;                  // r12
    *--sp = 0;                  // r13
    *--sp = 0;                  // r14
    *--sp = 0;                  // r15

    t->tid = tid++;
    t->ksp = sp;
    t->kstack_top = kstack + 4096;
    t->tsp = NULL;
    t->addr_space = paging_create_pml4();
    t->next = t;
    t->state = Ready;

    if (thread_list == NULL) {
        thread_list = t;
        t->next = t;
    } else {
        t->next = thread_list->next;
        thread_list->next = t;
    }

    return t;
}
