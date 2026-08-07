#define SYS_WRITE 1
#define SYS_GETPID 39
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_RT_SIGPENDING 127
#define SYS_KILL 62
#define SYS_EXIT 60

#define SIGSEGV 11
#define SIGUSR1 23
#define SIGUSR2 24

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef unsigned long sigset_t;

struct sigaction {
    void *sa_handler;
    sigset_t sa_mask;
    int sa_flags;
};

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("int $0x80"
                  : "=a"(ret)
                  : "D"(num), "S"(a1), "d"(a2), "c"(a3)
                  : "memory");
    return ret;
}

static void write_str(const char *s) {
    long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (long)s, n);
}

static void fail(const char *s) {
    write_str(s);
    syscall3(SYS_EXIT, 1, 0, 0);
    for (;;);
}

static volatile int got_usr1;

static void handler(int sig) {
    if (sig == SIGUSR1) {
        got_usr1++;
        write_str("sig_test: handler got SIGUSR1\n");
    } else {
        write_str("sig_test: handler unexpected signal\n");
    }
}

void _start() {
    struct sigaction act, old;
    sigset_t mask, pend;

    write_str("sig_test: start\n");

    act.sa_handler = (void *)handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    if (syscall3(SYS_RT_SIGACTION, SIGUSR1, (long)&act, (long)&old) != 0)
        fail("sig_test: FAIL sigaction\n");

    long pid = syscall3(SYS_GETPID, 0, 0, 0);
    if (syscall3(SYS_KILL, pid, SIGUSR1, 0) != 0)
        fail("sig_test: FAIL raise\n");
    if (got_usr1 != 1)
        fail("sig_test: FAIL handler did not run\n");
    write_str("sig_test: raise handler OK\n");

    act.sa_handler = (void *)1;
    if (syscall3(SYS_RT_SIGACTION, SIGUSR2, (long)&act, (long)&old) != 0)
        fail("sig_test: FAIL sigaction ignore\n");
    if (syscall3(SYS_KILL, pid, SIGUSR2, 0) != 0)
        fail("sig_test: FAIL raise ignored\n");
    write_str("sig_test: ignore OK\n");

    mask = 1UL << (SIGUSR1 - 1);
    if (syscall3(SYS_RT_SIGPROCMASK, SIG_BLOCK, (long)&mask, 0) != 0)
        fail("sig_test: FAIL sigprocmask block\n");
    syscall3(SYS_KILL, pid, SIGUSR1, 0);
    pend = 0;
    if (syscall3(SYS_RT_SIGPENDING, (long)&pend, 0, 0) != 0)
        fail("sig_test: FAIL sigpending\n");
    if (!(pend & (1UL << (SIGUSR1 - 1))))
        fail("sig_test: FAIL not pending\n");
    write_str("sig_test: blocked + pending OK\n");

    if (syscall3(SYS_RT_SIGPROCMASK, SIG_UNBLOCK, (long)&mask, 0) != 0)
        fail("sig_test: FAIL sigprocmask unblock\n");
    if (got_usr1 != 2)
        fail("sig_test: FAIL unblock did not deliver\n");
    write_str("sig_test: unblock delivered OK\n");

    if (syscall3(SYS_KILL, pid, SIGUSR1, 0) != 0)
        fail("sig_test: FAIL kill\n");
    if (got_usr1 != 3)
        fail("sig_test: FAIL kill not delivered\n");
    write_str("sig_test: kill self OK\n");

    write_str("sig_test: triggering SIGSEGV, expect termination\n");
    *(volatile int *)0 = 1;
    for (;;);
}
