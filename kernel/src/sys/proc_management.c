#include <stdint.h>
#include <fs/vfs.h>
#include <multitasking/sched.h>
#include <multitasking/proc.h>
#include <multitasking/thread.h>

typedef int pid_t;

void _exit(uint64_t exit_code) {
    struct pcb *p = sched_current_proc();
    if (p) {
        p->exit_code = exit_code;
    }

    current_tcb->state = Exited;

    schedule();

    for (;;) {
        asm volatile ("hlt");
    }
}

pid_t fork() {
    struct tcb *p = sched_current_proc();
    
}