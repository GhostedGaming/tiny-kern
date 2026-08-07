#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_EXIT 60
#define SYS_EXIT_GROUP 231

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                  : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                  : "rcx", "r11", "memory");
    return ret;
}

static void write_str(const char *s) {
    long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (long)s, n);
}

void _start() {
    write_str("input_test: start\n");
    write_str("input_test: type a line of text then press enter\n");

    char buf[256];
    long n = syscall3(SYS_READ, 0, (long)buf, sizeof(buf) - 1);
    if (n <= 0) {
        write_str("input_test: FAIL no input\n");
        syscall6(SYS_EXIT_GROUP, 1, 0, 0, 0, 0, 0);
        for (;;);
    }

    buf[n] = 0;
    write_str("input_test: received: ");
    syscall3(SYS_WRITE, 1, (long)buf, n);
    write_str("input_test: input OK\n");
    syscall6(SYS_EXIT_GROUP, 0, 0, 0, 0, 0, 0);
    for (;;);
}
