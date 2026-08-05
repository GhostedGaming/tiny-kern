#pragma once

extern struct tcb *current_tcb;

void schedule();
struct tcb *sched_current_thread();
struct pcb *sched_current_proc();
struct tcb *block_current();
void unblock(struct tcb *t);