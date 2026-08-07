#define SYS_WRITE 1
#define SYS_GETPID 39
#define SYS_EXIT 60

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                  : "rcx", "r11", "memory");
    return ret;
}

void _start() {
    const char *m1 = "syscall_test: start\n";
    syscall3(SYS_WRITE, 1, (long)m1, 21);

    long pid = syscall3(SYS_GETPID, 0, 0, 0);
    if (pid <= 0) {
        const char *err = "syscall_test: FAIL getpid\n";
        syscall3(SYS_WRITE, 1, (long)err, 26);
        syscall3(SYS_EXIT, 1, 0, 0);
        for (;;);
    }

    const char *m2 = "syscall_test: getpid OK\n";
    syscall3(SYS_WRITE, 1, (long)m2, 25);

    syscall3(SYS_EXIT, 0, 0, 0);
    for (;;);
}
