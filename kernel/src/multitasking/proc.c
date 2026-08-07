#include <stdint.h>
#include <stddef.h>
#include <mm/page.h>
#include <mm/frame.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <mm/memory.h>
#include <fs/vfs.h>
#include <multitasking/thread.h>
#include <multitasking/proc.h>

#define USER_HEAP_START 0x0000600000000000UL
#define USER_STACK_TOP 0x0000700000000000UL
#define USTACK_SIZE 0x10000

struct pcb *proc_list = NULL;
uint64_t proc_count = 0;

struct pcb *proc_create(void *entry) {
    struct pcb *p = (struct pcb *)kmalloc(sizeof(struct pcb));
    if (!p) {
        return NULL;
    }

    p->pid = proc_count++;
    p->t_count = 0;
    p->addr_space = paging_create_pml4();
    p->t = NULL;
    p->heap_begin = USER_HEAP_START;
    p->heap_end = USER_HEAP_START;
    p->exit_code = 0;
    p->stopped = 0;
    memset(&p->sigstate, 0, sizeof(p->sigstate));

    vfs_fd_table_init(p->fd_table, MAX_FDS);
    vfs_fd_table_setup_stdio(p->fd_table, MAX_FDS);

    void *ustack = vmm_map_region(
        (uint64_t *)p->addr_space,
        (void *)(USER_STACK_TOP - USTACK_SIZE),
        PAGE_WRITABLE | PAGE_USER,
        USTACK_SIZE / PAGE_SIZE
    );

    if (!ustack) {
        kfree(p);
        return NULL;
    }

    if (proc_list == NULL) {
        proc_list = p;
        p->next = p;
    } else {
        p->next = proc_list->next;
        proc_list->next = p;
    }

    struct tcb *t = create_thread(entry, p, ustack);
    if (!t) {
        kfree(p);
        return NULL;
    }

    return p;
}

struct pcb *proc_find(uint64_t pid) {
    if (!proc_list) {
        return NULL;
    }
    struct pcb *p = proc_list;
    do {
        if (p->pid == pid) {
            return p;
        }
        p = p->next;
    } while (p != proc_list);
    return NULL;
}

uintptr_t proc_sbrk(struct pcb *p, intptr_t increment) {
    uintptr_t old_end = p->heap_end;

    if (increment == 0) {
        return old_end;
    }

    if (increment > 0) {
        uint64_t bytes_needed = (uint64_t)increment;
        int pages_needed = (bytes_needed + PAGE_SIZE - 1) / PAGE_SIZE;

        void *mapped = vmm_map_region(
            (uint64_t *)p->addr_space,
            (void *)p->heap_end,
            PAGE_WRITABLE | PAGE_USER,
            pages_needed
        );

        if (!mapped) {
            return (uintptr_t)-1;
        }

        p->heap_end += pages_needed * PAGE_SIZE;
    } else {
        uintptr_t shrink_target = old_end + increment;

        linked_list_node_t *node = vmm_find_region(p->heap_end);
        if (node) {
            vmm_free_region((uint64_t *)p->addr_space, node);
        }

        p->heap_end = shrink_target;
    }

    return old_end;
}

void proc_destroy(struct pcb *p) {
    vfs_fd_table_close(p->fd_table, MAX_FDS);

    while (p->t != NULL) {
        destroy_thread(p->t);
    }

    if (proc_list) {
        if (proc_list->next == proc_list) {
            proc_list = NULL;
        } else {
            struct pcb *prev = proc_list;
            while (prev->next != p) {
                prev = prev->next;
            }
            prev->next = p->next;
            if (proc_list == p) {
                proc_list = p->next;
            }
        }
    }

    paging_destroy_address_space(p->addr_space);
    kfree(p);
}