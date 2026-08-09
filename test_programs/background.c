#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall2(long, scw, scw);
extern scw __do_syscall3(long, scw, scw, scw);

#define WNOHANG 1

static int kwait(int pid, int *st, int opts) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall3(SYS_WAITPID, pid, (scw)st, (scw)opts));
}

static void delay(void) {
    for (volatile long i = 0; i < 200000000L; i++);
}

int main(void) {
    printf("background: start pid=%d\n", getpid());

    int p1 = fork();
    if (p1 == 0) {
        _exit(11);
    }

    delay();

    int p2 = fork();
    if (p2 == 0) {
        delay();
        _exit(22);
    }

    delay();

    int st = 0;
    int r2 = kwait(p2, &st, 0);
    printf("background: waited p2=%d got=%d status=%d\n", p2, r2, st);

    int r1 = kwait(p1, &st, 0);
    printf("background: waited p1=%d got=%d status=%d\n", p1, r1, st);

    printf("background: done\n");
    return 0;
}
