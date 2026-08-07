#pragma once

#include <stdint.h>
#include <signal.h>

#define MAX_FDS 256

extern uint64_t process_count;

struct pcb {
    uint64_t pid;
    uint64_t t_count;
    uintptr_t addr_space;
    struct tcb *t;
    uintptr_t heap_begin;
    uintptr_t heap_end;
    uint64_t exit_code;
    uint8_t stopped;
    sigstate_t sigstate;
    struct pcb *next;
    struct vfs_file *fd_table[MAX_FDS];
};

extern struct pcb *proc_list;
extern uint64_t proc_count;

struct pcb *proc_create(void *entry);
struct pcb *proc_find(uint64_t pid);
void proc_destroy(struct pcb *p);
uintptr_t proc_sbrk(struct pcb *p, intptr_t increment);