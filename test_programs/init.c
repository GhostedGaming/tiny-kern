#define SYS_EXIT  0
#define SYS_WRITE 1

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

    for (;;) {
        asm volatile ("pause");
    }
}
