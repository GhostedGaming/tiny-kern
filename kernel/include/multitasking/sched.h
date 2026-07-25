#pragma once

extern struct tcb *current_tcb;

void schedule();
struct tcb *sched_current_thread();
struct tcb *block_current();
void unblock(struct tcb *t);