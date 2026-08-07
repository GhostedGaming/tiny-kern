#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_GETPID 39
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_MOUNT 165

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("int $0x80"
                  : "=a"(ret)
                  : "D"(num), "S"(a1), "d"(a2), "c"(a3)
                  : "memory");
    return ret;
}

void _start() {
    const char *msg = "init: running in usermode\n";
    syscall3(SYS_WRITE, 1, (long)msg, 27);
    long rc = syscall3(SYS_MOUNT, (long)"drive0", 0, 0);
    if (rc != 0) {
        const char *err = "init: mount drive0 failed\n";
        syscall3(SYS_WRITE, 1, (long)err, 25);
    }
    long pid = syscall3(SYS_FORK, 0, 0, 0);
    if (pid == 0) {
        char *argv[] = { "user_idle", 0 };
        char *envp[] = { 0 };
        syscall3(SYS_EXECVE, (long)"/ram/bins/user_idle", (long)argv, (long)envp);
        for (;;) {
            asm volatile ("pause");
        }
    }

    long pid2 = syscall3(SYS_FORK, 0, 0, 0);
    if (pid2 == 0) {
        char *argv[] = { "sig_test", 0 };
        char *envp[] = { 0 };
        syscall3(SYS_EXECVE, (long)"/ram/bins/sig_test", (long)argv, (long)envp);
        for (;;) {
            asm volatile ("pause");
        }
    }

    long pid3 = syscall3(SYS_FORK, 0, 0, 0);
    if (pid3 == 0) {
        char *argv[] = { "syscall_test", 0 };
        char *envp[] = { 0 };
        syscall3(SYS_EXECVE, (long)"/ram/bins/syscall_test", (long)argv, (long)envp);
        for (;;) {
            asm volatile ("pause");
        }
    }

    long pid4 = syscall3(SYS_FORK, 0, 0, 0);
    if (pid4 == 0) {
        char *argv[] = { "tier_a_test", 0 };
        char *envp[] = { 0 };
        syscall3(SYS_EXECVE, (long)"/ram/bins/tier_a_test", (long)argv, (long)envp);
        for (;;) {
            asm volatile ("pause");
        }
    }

    syscall3(SYS_EXIT, 0, 0, 0);
}
