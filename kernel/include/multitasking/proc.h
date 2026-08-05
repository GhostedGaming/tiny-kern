#pragma once

#include <stdint.h>

#define MAX_FDS 256

struct pcb {
    uint64_t pid;
    uint64_t t_count;
    uintptr_t addr_space;
    struct tcb *t;
    uintptr_t heap_begin;
    uintptr_t heap_end;
    uint64_t exit_code;
    struct pcb *next;
    struct vfs_file *fd_table[MAX_FDS];
};

struct pcb *proc_create(void *entry);
void proc_destroy(struct pcb *p);